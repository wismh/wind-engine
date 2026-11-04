#pragma once

// docs/tech/modules/Net.md

#include "net/snapshot_peer_header.h"
#include "net/snapshot_state.h"

#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_tick.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace engine::net {

class ReplicationRegistry;

// Bytes of a Snapshot message with no removed id and no record: id, tick, input report, and the two counts.
inline constexpr std::size_t kSnapshotHeaderBytes = 15;

// Every entity with `NetIdentity`, each registered component encoded, sorted by `NetId`. An entity whose bytes equal
// its bytes in `previous` shares `previous`'s blobs, so comparing it to anything sent earlier is one pointer compare.
[[nodiscard]] SnapshotState capture_snapshot(
        ecs::World& world, const ReplicationRegistry& registry, Tick tick, const SnapshotState* previous = nullptr);

// One entity record to send: the slots in `changed` with their values from `entity`, the slots in `lost` as gone.
struct SnapshotWrite {
    const EntitySnapshot* entity{nullptr};
    bool write_owner{false};
    std::uint32_t changed{0};
    std::uint32_t lost{0};
};

// The bytes `write` takes in the message.
[[nodiscard]] std::size_t snapshot_write_size(const SnapshotWrite& write) noexcept;

// The whole Snapshot message, starting with its `MessageId`.
[[nodiscard]] std::vector<std::uint8_t> encode_snapshot(Tick tick, const SnapshotPeerHeader& peer,
        std::span<const NetId> removed, std::span<const SnapshotWrite> writes);

// One entity record as received.
struct SnapshotRecord {
    NetId id{kInvalidNetId};
    std::optional<PeerId> owner{};
    std::uint32_t changed{0};
    std::uint32_t lost{0};
    // One per bit of `changed`, ascending.
    EntitySnapshot::Blobs blobs{};
};

// What one Snapshot message says, before it is put on top of the state before it.
struct SnapshotDelta {
    Tick tick{kNoTick};
    std::vector<NetId> removed{};
    std::vector<SnapshotRecord> records{};
};

// Reads the tick and the peer's input report after the `MessageId`.
[[nodiscard]] bool read_snapshot_header(ByteReader& reader, Tick& tick, SnapshotPeerHeader& peer);

// Reads the rest of a snapshot whose header said `tick`. Nullopt on any malformed input: truncation, trailing bytes,
// a slot at or above `slot_count`, a slot both changed and lost, or an id both removed and written.
[[nodiscard]] std::optional<SnapshotDelta> decode_snapshot(ByteReader& reader, Tick tick, std::size_t slot_count);

// `previous` (null: nothing) with `delta` on top: slots a record does not name keep their values, an entity no record
// names keeps everything, and a removed id that is not there is ignored (its create may have been lost). A record
// without an owner for an entity that is not there is impossible from a server that saw its create acknowledged:
// `strict` refuses the whole delta, otherwise that record is skipped.
[[nodiscard]] std::optional<SnapshotState> apply_snapshot(
        const SnapshotState* previous, const SnapshotDelta& delta, bool strict);

} // namespace engine::net
