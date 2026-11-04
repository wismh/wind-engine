#include "net/snapshot_codec.h"

#include <engine/net/net_messages.h>
#include <engine/net/network_identity.h>
#include <engine/net/replication_registry.h>

#include <algorithm>
#include <map>

namespace engine::net {

namespace {

constexpr std::uint8_t kFlagOwner = 1u << 0;
constexpr std::uint8_t kFlagRemoved = 1u << 1;
constexpr std::size_t kSlots = ReplicationRegistry::kMaxReplicated;

[[nodiscard]] bool same_component(const EntitySnapshot& a, const EntitySnapshot& b, std::size_t slot) {
    const auto x = a.component(slot);
    const auto y = b.component(slot);
    return std::equal(x.begin(), x.end(), y.begin(), y.end());
}

} // namespace

SnapshotState capture_snapshot(ecs::World& world, const ReplicationRegistry& registry, Tick tick) {
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
        for (std::size_t slot = 0; slot < registry.size(); ++slot) {
            ByteWriter bytes;
            if (registry.entry(slot).write(world, entity, bytes)) {
                record.mask |= std::uint32_t{1} << slot;
                record.components.emplace_back(bytes.buffer());
            }
        }
        state.entities.push_back(std::move(record));
    }
    std::sort(state.entities.begin(), state.entities.end(),
            [](const EntitySnapshot& a, const EntitySnapshot& b) { return a.id < b.id; });
    return state;
}

std::vector<std::uint8_t> encode_snapshot(
        const SnapshotState& current, const SnapshotState* baseline, const SnapshotPeerHeader& peer) {
    ByteWriter out;
    out.write_u16(kSnapshotMessageId);
    out.write_u32(current.tick);
    out.write_u32(baseline != nullptr ? baseline->tick : kNoTick);
    out.write_u32(peer.input_tick);
    out.write_i8(peer.input_slack);

    std::vector<NetId> removed;
    if (baseline != nullptr) {
        for (const EntitySnapshot& old : baseline->entities) {
            if (current.find(old.id) == nullptr) {
                removed.push_back(old.id);
            }
        }
    }
    out.write_u16(static_cast<std::uint16_t>(removed.size()));
    for (const NetId id : removed) {
        out.write_u32(id);
    }

    ByteWriter records;
    std::uint16_t count = 0;
    for (const EntitySnapshot& entity : current.entities) {
        const EntitySnapshot* old = baseline != nullptr ? baseline->find(entity.id) : nullptr;
        std::uint8_t flags = 0;
        std::uint32_t changed = 0;
        std::uint32_t lost = 0;
        if (old == nullptr) {
            flags |= kFlagOwner;
            changed = entity.mask;
        } else {
            if (old->owner != entity.owner) {
                flags |= kFlagOwner;
            }
            for (std::size_t slot = 0; slot < kSlots; ++slot) {
                if (entity.has(slot) && (!old->has(slot) || !same_component(entity, *old, slot))) {
                    changed |= std::uint32_t{1} << slot;
                }
            }
            lost = old->mask & ~entity.mask;
            if (lost != 0) {
                flags |= kFlagRemoved;
            }
            if (flags == 0 && changed == 0) {
                continue;
            }
        }
        records.write_u32(entity.id);
        records.write_u8(flags);
        if ((flags & kFlagOwner) != 0) {
            records.write_u64(entity.owner);
        }
        records.write_u32(changed);
        if ((flags & kFlagRemoved) != 0) {
            records.write_u32(lost);
        }
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
            if ((changed & (std::uint32_t{1} << slot)) == 0) {
                continue;
            }
            const auto blob = entity.component(slot);
            records.write_u16(static_cast<std::uint16_t>(blob.size()));
            records.append(blob);
        }
        ++count;
    }
    out.write_u16(count);
    out.append(records.bytes());
    return out.buffer();
}

bool read_snapshot_header(ByteReader& reader, Tick& tick, Tick& baseline_tick, SnapshotPeerHeader& peer) {
    const auto t = reader.read_u32();
    const auto b = reader.read_u32();
    const auto input_tick = reader.read_u32();
    const auto input_slack = reader.read_i8();
    if (!t.has_value() || !b.has_value() || !input_tick.has_value() || !input_slack.has_value() || *t == kNoTick ||
            (*b != kNoTick && *b >= *t)) {
        return false;
    }
    tick = *t;
    baseline_tick = *b;
    peer = SnapshotPeerHeader{.input_tick = *input_tick, .input_slack = *input_slack};
    return true;
}

std::optional<SnapshotState> decode_snapshot(ByteReader& reader, Tick tick, Tick baseline_tick,
        const SnapshotState* baseline, std::size_t slot_count) {
    if ((baseline_tick == kNoTick) != (baseline == nullptr)) {
        return std::nullopt;
    }
    if (baseline != nullptr && baseline->tick != baseline_tick) {
        return std::nullopt;
    }
    const std::uint32_t valid_slots =
            slot_count >= kSlots ? ~std::uint32_t{0} : (std::uint32_t{1} << slot_count) - 1u;

    std::map<NetId, EntitySnapshot> entities;
    if (baseline != nullptr) {
        for (const EntitySnapshot& entity : baseline->entities) {
            entities.emplace(entity.id, entity);
        }
    }

    const auto removed_count = reader.read_u16();
    if (!removed_count.has_value()) {
        return std::nullopt;
    }
    for (std::uint16_t i = 0; i < *removed_count; ++i) {
        const auto id = reader.read_u32();
        if (!id.has_value() || entities.erase(*id) == 0) {
            return std::nullopt;
        }
    }

    const auto count = reader.read_u16();
    if (!count.has_value()) {
        return std::nullopt;
    }
    for (std::uint16_t i = 0; i < *count; ++i) {
        const auto id = reader.read_u32();
        const auto flags = reader.read_u8();
        if (!id.has_value() || !flags.has_value() || *id == kInvalidNetId) {
            return std::nullopt;
        }
        std::optional<PeerId> owner;
        if ((*flags & kFlagOwner) != 0) {
            owner = reader.read_u64();
            if (!owner.has_value()) {
                return std::nullopt;
            }
        }
        const auto changed = reader.read_u32();
        if (!changed.has_value()) {
            return std::nullopt;
        }
        std::uint32_t lost = 0;
        if ((*flags & kFlagRemoved) != 0) {
            const auto mask = reader.read_u32();
            if (!mask.has_value()) {
                return std::nullopt;
            }
            lost = *mask;
        }
        if ((*changed & ~valid_slots) != 0 || (lost & ~valid_slots) != 0 || (*changed & lost) != 0) {
            return std::nullopt;
        }

        const auto existing = entities.find(*id);
        if (existing == entities.end() && !owner.has_value()) {
            return std::nullopt;
        }
        const EntitySnapshot old = existing != entities.end() ? existing->second : EntitySnapshot{.id = *id};

        EntitySnapshot next;
        next.id = *id;
        next.owner = owner.value_or(old.owner);
        next.mask = (old.mask & ~lost) | *changed;
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
            const std::uint32_t bit = std::uint32_t{1} << slot;
            if ((next.mask & bit) == 0) {
                continue;
            }
            if ((*changed & bit) != 0) {
                const auto size = reader.read_u16();
                if (!size.has_value()) {
                    return std::nullopt;
                }
                const auto blob = reader.read_bytes(*size);
                if (!blob.has_value()) {
                    return std::nullopt;
                }
                next.components.emplace_back(blob->begin(), blob->end());
            } else {
                const auto blob = old.component(slot);
                next.components.emplace_back(blob.begin(), blob.end());
            }
        }
        entities.insert_or_assign(*id, std::move(next));
    }

    if (!reader.is_ok() || reader.remaining() != 0) {
        return std::nullopt;
    }

    SnapshotState state;
    state.tick = tick;
    state.entities.reserve(entities.size());
    for (auto& [id, entity] : entities) {
        state.entities.push_back(std::move(entity));
    }
    return state;
}

} // namespace engine::net
