#include <engine/net/net_entity_map.h>

namespace engine::net {

bool NetEntityMap::bind(NetId net_id, ecs::Entity entity) {
    if (net_id == kInvalidNetId) {
        return false;
    }
    if (net_to_entity_.contains(net_id) || entity_to_net_.contains(entity)) {
        return false;
    }
    net_to_entity_[net_id] = entity;
    entity_to_net_[entity] = net_id;
    return true;
}

bool NetEntityMap::unbind(NetId net_id) {
    const auto it = net_to_entity_.find(net_id);
    if (it == net_to_entity_.end()) {
        return false;
    }
    entity_to_net_.erase(it->second);
    net_to_entity_.erase(it);
    return true;
}

bool NetEntityMap::unbind(ecs::Entity entity) {
    const auto it = entity_to_net_.find(entity);
    if (it == entity_to_net_.end()) {
        return false;
    }
    net_to_entity_.erase(it->second);
    entity_to_net_.erase(it);
    return true;
}

std::optional<ecs::Entity> NetEntityMap::find_entity(NetId net_id) const {
    const auto it = net_to_entity_.find(net_id);
    if (it != net_to_entity_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<NetId> NetEntityMap::find_net_id(ecs::Entity entity) const {
    const auto it = entity_to_net_.find(entity);
    if (it != entity_to_net_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool NetEntityMap::contains(NetId net_id) const {
    return net_to_entity_.contains(net_id);
}

bool NetEntityMap::contains(ecs::Entity entity) const {
    return entity_to_net_.contains(entity);
}

void NetEntityMap::clear() {
    net_to_entity_.clear();
    entity_to_net_.clear();
}

std::size_t NetEntityMap::size() const noexcept {
    return net_to_entity_.size();
}

bool NetEntityMap::empty() const noexcept {
    return net_to_entity_.empty();
}

bool bind_net_entity(ecs::World& world, ecs::Entity entity, NetId net_id) {
    if (!world.valid(entity) || net_id == kInvalidNetId) {
        return false;
    }
    auto& map = world.ctx<NetEntityMap>();
    if (!map.bind(net_id, entity)) {
        return false;
    }
    if (auto* existing = world.try_get<NetIdentity>(entity)) {
        existing->id = net_id;
    } else {
        world.emplace<NetIdentity>(entity, NetIdentity{.id = net_id});
    }
    return true;
}

bool unbind_net_entity(ecs::World& world, ecs::Entity entity) {
    if (!world.valid(entity)) {
        return false;
    }
    world.remove<NetIdentity>(entity);
    auto& map = world.ctx<NetEntityMap>();
    return map.unbind(entity);
}

std::optional<ecs::Entity> find_entity(const ecs::World& world, NetId net_id) {
    if (const auto* map = world.try_ctx<NetEntityMap>()) {
        return map->find_entity(net_id);
    }
    return std::nullopt;
}

std::optional<NetId> find_net_id(const ecs::World& world, ecs::Entity entity) {
    if (const auto* ident = world.try_get<NetIdentity>(entity)) {
        return ident->id;
    }
    if (const auto* map = world.try_ctx<NetEntityMap>()) {
        return map->find_net_id(entity);
    }
    return std::nullopt;
}

bool set_parent_by_net_id(ecs::World& world, NetId child, NetId parent, ecs::KeepWorld keep_world) {
    const auto child_entity = find_entity(world, child);
    const auto parent_entity = find_entity(world, parent);
    if (!child_entity || !parent_entity) {
        return false;
    }
    return ecs::set_parent(world, *child_entity, *parent_entity, keep_world);
}

std::optional<NetId> get_parent_net_id(const ecs::World& world, ecs::Entity entity) {
    const auto* parent = world.try_get<Parent>(entity);
    if (parent == nullptr) {
        return std::nullopt;
    }
    return find_net_id(world, parent->entity);
}

} // namespace engine::net
