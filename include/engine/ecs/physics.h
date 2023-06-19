#pragma once

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <glm/vec3.hpp>

namespace engine {

struct RigidBody {
    glm::vec3 velocity{0};
};

struct BoxCollider {
    glm::vec3 size{1.f, 1.f, 1.f};  // full extents; AABB centered on Transform.position
};

struct CollisionEvent {
    ecs::Entity a{};
    ecs::Entity b{};

    constexpr bool operator==(const CollisionEvent&) const noexcept = default;
};

void run_physics(ecs::World& world);

}
