#pragma once

// docs/tech/modules/Net.md

#include <engine/net/network_transport.h>

namespace engine::net {

// Sent on the server world when a client leaves, and on the client world when the connection ends.
struct NetPeerDisconnectedEvent {
    PeerId peer_id{kInvalidPeerId};
    DisconnectReason reason{DisconnectReason::None};
};

} // namespace engine::net
