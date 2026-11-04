#include "net/snapshot_codec.h"

#include <engine/net/net_messages.h>
#include <engine/net/network_identity.h>
#include <engine/net/replication_registry.h>

#include <algorithm>
#include <bit>
#include <memory>

namespace engine::net {

namespace {

constexpr std::uint8_t kFlagOwner = 1u << 0;
constexpr std::uint8_t kFlagRemoved = 1u << 1;
constexpr std::size_t kSlots = ReplicationRegistry::kMaxReplicated;

[[nodiscard]] const EntitySnapshot* find_in(const SnapshotState* state, NetId id) noexcept {
    return state != nullptr ? state->find(id) : nullptr;
}

} // namespace

SnapshotState capture_snapshot(
        ecs::World& world, const ReplicationRegistry& registry, Tick tick, const SnapshotState* previous) {
    SnapshotState state;
    state.tick = tick;
    for (const ecs::Entity entity : world.view<NetIdentity>()) {
        const NetIdentity& identity = world.get<NetIdentity>(entity);
        if (identity.id == kInvalidNetId) {
            continue;
        }
        EntitySnapshot record;
        record.id = identity.id;
        record.owner = identity.owner;
        EntitySnapshot::Blobs blobs;
        for (std::size_t slot = 0; slot < registry.size(); ++slot) {
            ByteWriter bytes;
            if (registry.entry(slot).write(world, entity, bytes)) {
                record.mask |= std::uint32_t{1} << slot;
                blobs.emplace_back(bytes.buffer());
            }
        }
        const EntitySnapshot* old = find_in(previous, identity.id);
        if (old != nullptr && old->mask == record.mask && old->components != nullptr && *old->components == blobs) {
            record.components = old->components;
        } else {
            record.components = std::make_shared<const EntitySnapshot::Blobs>(std::move(blobs));
        }
        state.entities.push_back(std::move(record));
    }
    std::sort(state.entities.begin(), state.entities.end(),
            [](const EntitySnapshot& a, const EntitySnapshot& b) { return a.id < b.id; });
    return state;
}

std::size_t snapshot_write_size(const SnapshotWrite& write) noexcept {
    std::size_t size = 4u + 1u + 4u;
    if (write.write_owner) {
        size += 8u;
    }
    if (write.lost != 0) {
        size += 4u;
    }
    for (std::uint32_t bits = write.changed; bits != 0; bits &= bits - 1u) {
        const auto slot = static_cast<std::size_t>(std::countr_zero(bits));
        size += 2u + write.entity->component(slot).size();
    }
    return size;
}

std::vector<std::uint8_t> encode_snapshot(Tick tick, const SnapshotPeerHeader& peer, std::span<const NetId> removed,
        std::span<const SnapshotWrite> writes) {
    std::size_t size = kSnapshotHeaderBytes + removed.size() * 4u;
    for (const SnapshotWrite& write : writes) {
        size += snapshot_write_size(write);
    }
    ByteWriter out(size);
    out.write_u16(kSnapshotMessageId);
    out.write_u32(tick);
    out.write_u32(peer.input_tick);
    out.write_i8(peer.input_slack);
    out.write_u16(static_cast<std::uint16_t>(removed.size()));
    for (const NetId id : removed) {
        out.write_u32(id);
    }
    out.write_u16(static_cast<std::uint16_t>(writes.size()));
    for (const SnapshotWrite& write : writes) {
        std::uint8_t flags = 0;
        if (write.write_owner) {
            flags |= kFlagOwner;
        }
        if (write.lost != 0) {
            flags |= kFlagRemoved;
        }
        out.write_u32(write.entity->id);
        out.write_u8(flags);
        if (write.write_owner) {
            out.write_u64(write.entity->owner);
        }
        out.write_u32(write.changed);
        if (write.lost != 0) {
            out.write_u32(write.lost);
        }
        for (std::uint32_t bits = write.changed; bits != 0; bits &= bits - 1u) {
            const auto blob = write.entity->component(static_cast<std::size_t>(std::countr_zero(bits)));
            out.write_u16(static_cast<std::uint16_t>(blob.size()));
            out.append(blob);
        }
    }
    return out.buffer();
}

bool read_snapshot_header(ByteReader& reader, Tick& tick, SnapshotPeerHeader& peer) {
    const auto t = reader.read_u32();
    const auto input_tick = reader.read_u32();
    const auto input_slack = reader.read_i8();
    if (!t.has_value() || !input_tick.has_value() || !input_slack.has_value() || *t == kNoTick) {
        return false;
    }
    tick = *t;
    peer = SnapshotPeerHeader{.input_tick = *input_tick, .input_slack = *input_slack};
    return true;
}

std::optional<SnapshotDelta> decode_snapshot(ByteReader& reader, Tick tick, std::size_t slot_count) {
    const std::uint32_t valid_slots = slot_count >= kSlots ? ~std::uint32_t{0} : (std::uint32_t{1} << slot_count) - 1u;

    SnapshotDelta delta;
    delta.tick = tick;

    const auto removed_count = reader.read_u16();
    if (!removed_count.has_value()) {
        return std::nullopt;
    }
    delta.removed.reserve(*removed_count);
    for (std::uint16_t i = 0; i < *removed_count; ++i) {
        const auto id = reader.read_u32();
        if (!id.has_value()) {
            return std::nullopt;
        }
        delta.removed.push_back(*id);
    }
    std::vector<NetId> removed_sorted = delta.removed;
    std::sort(removed_sorted.begin(), removed_sorted.end());

    const auto count = reader.read_u16();
    if (!count.has_value()) {
        return std::nullopt;
    }
    delta.records.reserve(*count);
    for (std::uint16_t i = 0; i < *count; ++i) {
        const auto id = reader.read_u32();
        const auto flags = reader.read_u8();
        if (!id.has_value() || !flags.has_value() || *id == kInvalidNetId ||
                std::binary_search(removed_sorted.begin(), removed_sorted.end(), *id)) {
            return std::nullopt;
        }
        SnapshotRecord record;
        record.id = *id;
        if ((*flags & kFlagOwner) != 0) {
            record.owner = reader.read_u64();
            if (!record.owner.has_value()) {
                return std::nullopt;
            }
        }
        const auto changed = reader.read_u32();
        if (!changed.has_value()) {
            return std::nullopt;
        }
        record.changed = *changed;
        if ((*flags & kFlagRemoved) != 0) {
            const auto lost = reader.read_u32();
            if (!lost.has_value()) {
                return std::nullopt;
            }
            record.lost = *lost;
        }
        if ((record.changed & ~valid_slots) != 0 || (record.lost & ~valid_slots) != 0 ||
                (record.changed & record.lost) != 0) {
            return std::nullopt;
        }
        for (std::uint32_t bits = record.changed; bits != 0; bits &= bits - 1u) {
            const auto size = reader.read_u16();
            if (!size.has_value()) {
                return std::nullopt;
            }
            const auto blob = reader.read_bytes(*size);
            if (!blob.has_value()) {
                return std::nullopt;
            }
            record.blobs.emplace_back(blob->begin(), blob->end());
        }
        delta.records.push_back(std::move(record));
    }

    if (!reader.is_ok() || reader.remaining() != 0) {
        return std::nullopt;
    }
    return delta;
}

std::optional<SnapshotState> apply_snapshot(const SnapshotState* previous, const SnapshotDelta& delta, bool strict) {
    SnapshotState state;
    state.tick = delta.tick;
    if (previous != nullptr) {
        state.entities = previous->entities;
    }
    if (!delta.removed.empty()) {
        std::vector<NetId> removed = delta.removed;
        std::sort(removed.begin(), removed.end());
        std::erase_if(state.entities, [&removed](const EntitySnapshot& entity) {
            return std::binary_search(removed.begin(), removed.end(), entity.id);
        });
    }

    const std::size_t held = state.entities.size();
    for (const SnapshotRecord& record : delta.records) {
        const auto end = state.entities.begin() + static_cast<std::ptrdiff_t>(held);
        const auto at = std::lower_bound(state.entities.begin(), end, record.id,
                [](const EntitySnapshot& entity, NetId id) { return entity.id < id; });
        const bool known = at != end && at->id == record.id;
        if (!known && !record.owner.has_value()) {
            if (strict) {
                return std::nullopt;
            }
            continue;
        }

        EntitySnapshot next;
        next.id = record.id;
        next.owner = record.owner.value_or(known ? at->owner : kServerPeerId);
        const std::uint32_t old_mask = known ? at->mask : 0u;
        next.mask = (old_mask & ~record.lost) | record.changed;
        auto blobs = std::make_shared<EntitySnapshot::Blobs>();
        blobs->reserve(static_cast<std::size_t>(std::popcount(next.mask)));
        std::size_t written = 0;
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
            const std::uint32_t bit = std::uint32_t{1} << slot;
            if ((next.mask & bit) == 0) {
                continue;
            }
            if ((record.changed & bit) != 0) {
                blobs->push_back(record.blobs[written++]);
            } else {
                const auto blob = at->component(slot);
                blobs->emplace_back(blob.begin(), blob.end());
            }
        }
        next.components = std::move(blobs);
        if (known) {
            *at = std::move(next);
        } else {
            state.entities.push_back(std::move(next));
        }
    }
    if (state.entities.size() != held) {
        std::stable_sort(state.entities.begin(), state.entities.end(),
                [](const EntitySnapshot& a, const EntitySnapshot& b) { return a.id < b.id; });
        // The same new entity twice in one message: the last record wins.
        const auto duplicate = std::unique(state.entities.rbegin(), state.entities.rend(),
                [](const EntitySnapshot& a, const EntitySnapshot& b) { return a.id == b.id; });
        state.entities.erase(state.entities.begin(), duplicate.base());
    }
    return state;
}

} // namespace engine::net
