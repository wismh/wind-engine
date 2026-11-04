#pragma once

// docs/tech/modules/Net.md

#include <engine/net/net_interest.h>
#include <engine/net/net_messages.h>

#include <cstddef>
#include <cstdint>

namespace engine::net {

struct NetServerConfig {
    // Ticks between snapshots. 2 at the 60 Hz fixed step is 30 snapshots a second. 0 counts as 1.
    std::uint16_t snapshot_interval{2};
    // The largest Snapshot message, so that it stays one UDP packet and is not lost whole with any piece of it.
    // Clamped to 64..60000. See `kSnapshotMtuBudget`.
    std::size_t snapshot_budget_bytes{kSnapshotMtuBudget};
    // Everything the engine sends one peer: snapshots first, then queued payloads. At least 1 KiB/s. The default stays
    // under GameNetworkingSockets' own 256 KiB/s send rate.
    std::size_t peer_bytes_per_second{192 * 1024};
    // The part of that rate snapshots may not take, so payloads always move. Clamped to 0..0.9.
    float payload_min_share{0.1f};
    // Priority a queued payload gains per second of waiting, so that none waits forever. 0 turns that off.
    float payload_aging_per_second{1.0f};
    // No payload bytes go to a transport already holding this many unsent bytes for the peer: whatever sits there
    // delays the next snapshot behind it.
    std::size_t transport_backlog_bytes{8 * 1024};
    NetInterest interest{};
};

} // namespace engine::net
