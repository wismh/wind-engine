#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <glm/vec3.hpp>

#include <cstdint>

namespace engine {

struct RigidBody2D {
    glm::vec3 velocity{0};
};

struct BoxCollider2D {
    glm::vec3 size{1.f, 1.f, 1.f};   // full extents; AABB centered on Transform.position
    uint32_t layer{1};               // bitmask: what this collider is
    uint32_t mask{0xFFFFFFFFu};      // bitmask: what this collider detects
    bool is_trigger{false};
};

struct CircleCollider2D {
    float radius{0.5f};              // centered on Transform.position
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
};

enum class CollisionPhase {
    Enter,
    Stay,
    Exit,
};

struct CollisionEvent2D {
    ecs::Entity a{};
    ecs::Entity b{};
    CollisionPhase phase{CollisionPhase::Enter};
    bool is_trigger{false};  // true if either collider in the pair is a trigger

    constexpr bool operator==(const CollisionEvent2D&) const noexcept = default;
};

void run_physics_2d(ecs::World& world);

}
