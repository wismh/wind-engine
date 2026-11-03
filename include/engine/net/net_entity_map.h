#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/net/network_identity.h>

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::net {

// Two-way map between `NetId` and this machine's `ecs::Entity`. Lives in `world.ctx<NetEntityMap>()`.
class NetEntityMap {
public:
    NetEntityMap() = default;

    bool bind(NetId net_id, ecs::Entity entity);
    bool unbind(NetId net_id);
    bool unbind(ecs::Entity entity);

    // Unbinds every entry whose entity is no longer valid in `world` and returns their ids.
    std::vector<NetId> unbind_dead(const ecs::World& world);

    [[nodiscard]] std::optional<ecs::Entity> find_entity(NetId net_id) const;
    [[nodiscard]] std::optional<NetId> find_net_id(ecs::Entity entity) const;

    [[nodiscard]] bool contains(NetId net_id) const;
    [[nodiscard]] bool contains(ecs::Entity entity) const;

    void clear();

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    std::unordered_map<NetId, ecs::Entity> net_to_entity_{};
    std::unordered_map<ecs::Entity, NetId> entity_to_net_{};
};

// Binds `entity` to `net_id` in `world.ctx<NetEntityMap>()` and gives it `NetIdentity{net_id, owner}`.
bool bind_net_entity(ecs::World& world, ecs::Entity entity, NetId net_id, PeerId owner = kServerPeerId);
// Removes `NetIdentity` and the map entry.
bool unbind_net_entity(ecs::World& world, ecs::Entity entity);

// Through `NetEntityMap`; nullopt for an unknown id or a dead entity.
[[nodiscard]] std::optional<ecs::Entity> find_entity(const ecs::World& world, NetId net_id);
// The entity's `NetIdentity::id`; nullopt when it has none.
[[nodiscard]] std::optional<NetId> find_net_id(const ecs::World& world, ecs::Entity entity);

} // namespace engine::net
