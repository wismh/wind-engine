#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <functional>

namespace engine::net {

// Optional hooks of a replicated component. An empty hook takes the default.
template<typename T>
struct ReplicatedOptions {
    // Value between two snapshots at `t` in [0, 1]. Empty: the value steps (the older snapshot's value holds
    // until the timeline reaches the newer one).
    std::function<T(const T& from, const T& to, float t)> interpolate{};
    // Puts a received value on the entity. Empty: assign, or emplace when the entity has none.
    std::function<void(ecs::World&, ecs::Entity, const T&)> apply{};
    // Takes the component off when the server's entity lost it. Empty: `World::remove<T>`.
    std::function<void(ecs::World&, ecs::Entity)> remove{};
};

} // namespace engine::net
