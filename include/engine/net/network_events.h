#pragma once

#include <engine/net/network_transport.h>

namespace engine::net {

struct NetPeerConnectedEvent {
    PeerId peer_id{kInvalidPeerId};
};

struct NetPeerDisconnectedEvent {
    PeerId peer_id{kInvalidPeerId};
    DisconnectReason reason{DisconnectReason::None};
};

} // namespace engine::net
