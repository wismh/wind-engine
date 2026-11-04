#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/net/network_identity.h>

namespace engine::net {

// Sent on the client world right before a replicated entity is destroyed: it left the server's snapshot and
// the interpolation timeline passed that tick, or the connection ended. `entity` is dead when a reader sees it.
struct NetEntityDespawnedEvent {
    ecs::Entity entity{};
    NetId net_id{kInvalidNetId};
};

} // namespace engine::net
