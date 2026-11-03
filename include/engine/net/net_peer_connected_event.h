#pragma once

// docs/tech/modules/Net.md

#include <engine/net/network_transport.h>

namespace engine::net {

// Sent on the server world when a client connects, and on the client world when the server accepts it.
struct NetPeerConnectedEvent {
    PeerId peer_id{kInvalidPeerId};
};

} // namespace engine::net
