#pragma once

#include <engine/net/network_transport.h>

#include <cstdint>

namespace engine::net {

using NetId = std::uint32_t;
inline constexpr NetId kInvalidNetId = 0;

struct NetIdentity {
    NetId id{kInvalidNetId};
    PeerId owner_peer_id{kInvalidPeerId};
    bool is_local_authority{false};
};

} // namespace engine::net
