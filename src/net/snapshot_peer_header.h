#pragma once

// docs/tech/modules/Net.md

#include <engine/net/net_messages.h>
#include <engine/net/net_tick.h>

#include <cstdint>

namespace engine::net {

// The part of a snapshot header that differs per peer: how the server is doing with that peer's inputs.
struct SnapshotPeerHeader {
    // The newest tick at which the server applied a real (not repeated) input of this peer.
    Tick input_tick{kNoTick};
    // How early this peer's inputs arrive: the smallest (newest input tick of a message - the tick the server runs
    // next) over the Input messages received since this peer's previous snapshot, clamped to [-127, 127].
    // `kNoInputSlack` when none arrived.
    std::int8_t input_slack{kNoInputSlack};

    bool operator==(const SnapshotPeerHeader&) const = default;
};

} // namespace engine::net
