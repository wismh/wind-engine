#pragma once

#include <engine/ecs/entity.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/world.h>
#include <engine/net/net_id.h>

#include <cstddef>
#include <optional>
#include <unordered_map>

namespace engine::net {

class NetEntityMap {
public:
    NetEntityMap() = default;

    bool bind(NetId net_id, ecs::Entity entity);
    bool unbind(NetId net_id);
    bool unbind(ecs::Entity entity);

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

// Convenience helpers operating on ecs::World context
bool bind_net_entity(ecs::World& world, ecs::Entity entity, NetId net_id);
bool unbind_net_entity(ecs::World& world, ecs::Entity entity);

[[nodiscard]] std::optional<ecs::Entity> find_entity(const ecs::World& world, NetId net_id);
[[nodiscard]] std::optional<NetId> find_net_id(const ecs::World& world, ecs::Entity entity);

// Hierarchy translation helpers
bool set_parent_by_net_id(ecs::World& world, NetId child, NetId parent, ecs::KeepWorld keep_world = ecs::KeepWorld::No);
[[nodiscard]] std::optional<NetId> get_parent_net_id(const ecs::World& world, ecs::Entity entity);

} // namespace engine::net
