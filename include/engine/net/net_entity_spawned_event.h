#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/net/network_identity.h>

namespace engine::net {

// Sent on the client world when a replicated entity appears on the interpolation timeline. Its replicated
// components are already applied; the game adds what is local only (sprite, mesh, sound).
struct NetEntitySpawnedEvent {
    ecs::Entity entity{};
    NetId net_id{kInvalidNetId};
};

} // namespace engine::net
