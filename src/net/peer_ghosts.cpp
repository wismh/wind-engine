#include "net/peer_ghosts.h"

#include "net/snapshot_codec.h"

#include <engine/ecs/hierarchy.h>
#include <engine/net/net_messages.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace engine::net {

namespace {

struct Candidate {
    std::size_t ghost{0};
    const RelevantEntity* relevant{nullptr};
    SnapshotWrite write{};
    std::size_t size{0};
    bool selected{false};
};

[[nodiscard]] bool same_slot(const EntitySnapshot& a, const EntitySnapshot& b, std::size_t slot) noexcept {
    const bool has_a = a.has(slot);
    if (has_a != b.has(slot)) {
        return false;
    }
    if (!has_a || a.components == b.components) {
        return true;
    }
    const auto x = a.component(slot);
    const auto y = b.component(slot);
    return std::equal(x.begin(), x.end(), y.begin(), y.end());
}

[[nodiscard]] bool is_clean(const PeerGhosts::Ghost& ghost, const EntitySnapshot& current) noexcept {
    return ghost.written && ghost.confirmed >= ghost.max_since && ghost.latest.mask == current.mask &&
           ghost.latest.owner == current.owner &&
           (current.mask == 0 || ghost.latest.components == current.components);
}

// What `current` needs written to bring the peer up to it: every slot whose value is new, and every slot whose value
// has not been acknowledged, which also covers a value that came back while a different one was still in flight.
[[nodiscard]] SnapshotWrite dirty_write(
        const PeerGhosts::Ghost& ghost, const EntitySnapshot& current, std::size_t slot_count) noexcept {
    SnapshotWrite write{.entity = &current};
    write.write_owner =
            !ghost.written || ghost.latest.owner != current.owner || ghost.confirmed < ghost.owner_since;
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
        const bool unconfirmed = ghost.confirmed < ghost.since[slot];
        if (unconfirmed || !same_slot(ghost.latest, current, slot)) {
            const std::uint32_t bit = std::uint32_t{1} << slot;
            if (current.has(slot)) {
                write.changed |= bit;
            } else {
                write.lost |= bit;
            }
        }
    }
    return write;
}

void commit_write(PeerGhosts::Ghost& ghost, const EntitySnapshot& current, std::size_t slot_count, Tick tick) {
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
        if (!same_slot(ghost.latest, current, slot)) {
            ghost.since[slot] = tick;
        }
    }
    if (!ghost.written || ghost.latest.owner != current.owner) {
        ghost.owner_since = tick;
    }
    ghost.max_since = ghost.owner_since;
    for (const Tick since : ghost.since) {
        ghost.max_since = std::max(ghost.max_since, since);
    }
    ghost.latest = current;
    ghost.written = true;
    ghost.accumulator = 0.0f;
}

} // namespace

PeerGhosts::Ghost* PeerGhosts::find(NetId id) noexcept {
    const auto at = std::lower_bound(
            ghosts_.begin(), ghosts_.end(), id, [](const Ghost& ghost, NetId value) { return ghost.id < value; });
    return at != ghosts_.end() && at->id == id ? &*at : nullptr;
}

PeerGhosts::Built PeerGhosts::build(Tick tick, const SnapshotPeerHeader& header,
        std::span<const RelevantEntity> relevant, std::size_t budget, std::size_t slot_count,
        const EntityWeight& weight) {
    std::erase_if(sent_, [tick](const Sent& sent) { return sent.tick + kSnapshotWindowTicks <= tick; });

    // Walk the table and the relevant set together: what left starts leaving, what came back starts over.
    std::vector<Ghost> next;
    next.reserve(std::max(ghosts_.size(), relevant.size()));
    std::vector<Candidate> candidates;
    std::size_t old_index = 0;
    std::size_t new_index = 0;
    while (old_index < ghosts_.size() || new_index < relevant.size()) {
        const bool have_old = old_index < ghosts_.size();
        const bool have_new = new_index < relevant.size();
        const NetId old_id = have_old ? ghosts_[old_index].id : std::numeric_limits<NetId>::max();
        const NetId new_id = have_new ? relevant[new_index].entity->id : std::numeric_limits<NetId>::max();
        if (have_old && (!have_new || old_id < new_id)) {
            Ghost& ghost = ghosts_[old_index++];
            if (!ghost.written) {
                continue; // The peer never heard of it.
            }
            if (!ghost.leaving) {
                ghost.leaving = true;
                ghost.leave_since = tick;
                ghost.last_listed = kNoTick;
            }
            next.push_back(std::move(ghost));
            continue;
        }
        Ghost ghost;
        if (have_old && old_id == new_id) {
            ghost = std::move(ghosts_[old_index++]);
            if (ghost.leaving) {
                // The peer may already have dropped it. Everything is new again, and an ack of a snapshot from before
                // this tick says nothing about it.
                ghost.leaving = false;
                std::fill(ghost.since.begin(), ghost.since.end(), tick);
                ghost.owner_since = tick;
                ghost.max_since = tick;
            }
        } else {
            ghost.id = new_id;
            ghost.since.assign(slot_count, kNoTick);
        }
        const RelevantEntity& entity = relevant[new_index++];
        if (is_clean(ghost, *entity.entity)) {
            ghost.accumulator = 0.0f;
        } else {
            Candidate candidate{.ghost = next.size(), .relevant = &entity};
            candidate.write = dirty_write(ghost, *entity.entity, slot_count);
            candidate.size = snapshot_write_size(candidate.write);
            candidates.push_back(candidate);
        }
        next.push_back(std::move(ghost));
    }
    ghosts_ = std::move(next);

    std::vector<std::size_t> leaving;
    for (std::size_t i = 0; i < ghosts_.size(); ++i) {
        if (ghosts_[i].leaving) {
            leaving.push_back(i);
        }
    }

    std::size_t total = kSnapshotHeaderBytes + leaving.size() * 4u;
    for (const Candidate& candidate : candidates) {
        total += candidate.size;
    }

    std::vector<std::size_t> listed;
    if (total <= budget) {
        for (Candidate& candidate : candidates) {
            candidate.selected = true;
        }
        listed = leaving;
    } else {
        const auto saturating_sub = [](std::size_t a, std::size_t b) { return a > b ? a - b : 0u; };
        std::size_t remaining = saturating_sub(budget, kSnapshotHeaderBytes);
        std::vector<std::size_t> order;
        order.reserve(candidates.size());
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            Candidate& candidate = candidates[i];
            if (candidate.relevant->mandatory) {
                candidate.selected = true;
                remaining = saturating_sub(remaining, candidate.size);
            } else {
                ghosts_[candidate.ghost].accumulator += weight(*candidate.relevant);
                order.push_back(i);
            }
        }
        std::stable_sort(order.begin(), order.end(), [this, &candidates](std::size_t a, std::size_t b) {
            return ghosts_[candidates[a].ghost].accumulator > ghosts_[candidates[b].ghost].accumulator;
        });

        // A new child goes out with every ancestor that was never written, so its Parent resolves.
        const auto find_candidate = [&candidates](NetId id) -> Candidate* {
            const auto at = std::lower_bound(candidates.begin(), candidates.end(), id,
                    [](const Candidate& candidate, NetId value) { return candidate.relevant->entity->id < value; });
            return at != candidates.end() && at->relevant->entity->id == id ? &*at : nullptr;
        };
        const auto select = [&](Candidate& first, bool force) {
            std::vector<Candidate*> chain{&first};
            std::size_t cost = first.size;
            const Candidate* walk = &first;
            for (int depth = 0; depth < ecs::kMaxHierarchyDepth; ++depth) {
                if (ghosts_[walk->ghost].written || walk->relevant->parent == kInvalidNetId) {
                    break;
                }
                Candidate* parent = find_candidate(walk->relevant->parent);
                if (parent == nullptr || parent->selected) {
                    break;
                }
                chain.push_back(parent);
                cost += parent->size;
                walk = parent;
            }
            if (!force && cost > remaining) {
                return;
            }
            for (Candidate* candidate : chain) {
                candidate->selected = true;
            }
            remaining = saturating_sub(remaining, cost);
        };

        // The top of the order always goes out, so a record larger than what is left cannot wait forever.
        std::size_t next_order = 0;
        if (!order.empty()) {
            select(candidates[order[0]], /*force=*/true);
            next_order = 1;
        }

        // Removals, least recently listed first. They leave room for records while any wait.
        std::sort(leaving.begin(), leaving.end(), [this](std::size_t a, std::size_t b) {
            return ghosts_[a].last_listed != ghosts_[b].last_listed ? ghosts_[a].last_listed < ghosts_[b].last_listed
                                                                    : ghosts_[a].id < ghosts_[b].id;
        });
        const bool records_wait = next_order < order.size();
        const std::size_t removal_bytes = records_wait ? remaining / 4u : remaining;
        const std::size_t removal_count = std::min(leaving.size(), removal_bytes / 4u);
        listed.assign(leaving.begin(), leaving.begin() + static_cast<std::ptrdiff_t>(removal_count));
        remaining -= removal_count * 4u;

        for (; next_order < order.size(); ++next_order) {
            Candidate& candidate = candidates[order[next_order]];
            if (!candidate.selected) {
                select(candidate, /*force=*/false);
            }
        }
    }

    Sent sent{.tick = tick};
    std::vector<SnapshotWrite> writes;
    Built built;
    built.relevant = relevant.size();
    for (const Candidate& candidate : candidates) {
        if (candidate.selected) {
            writes.push_back(candidate.write);
            sent.written.push_back(candidate.write.entity->id);
        } else {
            ++built.deferred;
        }
    }
    sent.removed.reserve(listed.size());
    for (const std::size_t index : listed) {
        ghosts_[index].last_listed = tick;
        sent.removed.push_back(ghosts_[index].id);
    }
    std::sort(sent.removed.begin(), sent.removed.end());

    built.bytes = encode_snapshot(tick, header, sent.removed, writes);

    for (const Candidate& candidate : candidates) {
        if (candidate.selected) {
            commit_write(ghosts_[candidate.ghost], *candidate.write.entity, slot_count, tick);
        }
    }
    sent_.push_back(std::move(sent));
    return built;
}

void PeerGhosts::acknowledge(Tick tick) {
    const auto at = std::find_if(sent_.begin(), sent_.end(), [tick](const Sent& sent) { return sent.tick == tick; });
    if (at == sent_.end()) {
        return;
    }
    acked_ = std::max(acked_, tick);
    for (const NetId id : at->written) {
        if (Ghost* ghost = find(id)) {
            ghost->confirmed = std::max(ghost->confirmed, tick);
        }
    }
    bool gone = false;
    for (const NetId id : at->removed) {
        Ghost* ghost = find(id);
        if (ghost != nullptr && ghost->leaving && tick >= ghost->leave_since) {
            ghost->written = false;
            gone = true;
        }
    }
    if (gone) {
        std::erase_if(ghosts_, [](const Ghost& ghost) { return ghost.leaving && !ghost.written; });
    }
    sent_.erase(at);
}

} // namespace engine::net
