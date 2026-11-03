#pragma once

// docs/tech/modules/Net.md

#include "net/snapshot_state.h"

#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_tick.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::net {

class ReplicationRegistry;

// Every entity with `NetIdentity`, each registered component encoded, sorted by `NetId`.
[[nodiscard]] SnapshotState capture_snapshot(ecs::World& world, const ReplicationRegistry& registry, Tick tick);

// The whole Snapshot message, starting with its `MessageId`. A null `baseline` makes a full snapshot.
[[nodiscard]] std::vector<std::uint8_t> encode_snapshot(const SnapshotState& current, const SnapshotState* baseline);

// Reads the tick and baseline tick after the `MessageId`.
[[nodiscard]] bool read_snapshot_header(ByteReader& reader, Tick& tick, Tick& baseline_tick);

// Reads the rest of a snapshot whose header said `tick` and `baseline_tick`. `baseline` must be the state at
// `baseline_tick` (null when that is `kNoTick`). Nullopt on any malformed input: truncation, trailing bytes, a
// slot at or above `slot_count`, a new entity without an owner, or a baseline that does not match.
[[nodiscard]] std::optional<SnapshotState> decode_snapshot(ByteReader& reader, Tick tick, Tick baseline_tick,
        const SnapshotState* baseline, std::size_t slot_count);

} // namespace engine::net
