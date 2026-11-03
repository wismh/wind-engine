#pragma once

// docs/tech/modules/Net.md

#include <engine/net/network_transport.h>

#include <cstdint>

namespace engine::net {

// Stable id of a replicated entity, the same on every machine. Valid when non-zero.
using NetId = std::uint32_t;
inline constexpr NetId kInvalidNetId = 0;

// The component a replicated entity carries. `owner` is the server's id of the owning peer
// (`kServerPeerId` for server-owned entities); a client compares it with `NetClient::local_peer`.
struct NetIdentity {
    NetId id{kInvalidNetId};
    PeerId owner{kServerPeerId};
};

} // namespace engine::net
