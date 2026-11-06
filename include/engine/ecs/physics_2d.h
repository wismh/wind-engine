#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/entity.h>
#include <engine/ecs/physics.h>
#include <engine/ecs/world.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace engine {

struct RigidBody2D {
    glm::vec3 velocity{0};  // Kinematic: parent space. Dynamic: world XY; z is world z.
    BodyType type{BodyType::Kinematic};
    float mass{1.f};  // Dynamic only. Non-positive or non-finite uses kMinDynamicMass.
    float gravity_scale{1.f};
    // Radians/sec about world +Z. Kinematic and static store it. Only a dynamic body integrates it,
    // and only a dynamic body is written by the solver.
    float angular_velocity{0.f};
    // Dynamic only. Kinematic and static ignore it. A missing serialized key loads false.
    bool sleeping{false};
};

// Not a component. world.ctx<Physics2DConfig>() default-constructs it (gravity zero) on first use.
struct Physics2DConfig {
    glm::vec3 gravity{0.f};  // world space
};

struct BoxCollider2D {
    // Full extents in the box's local XY. Not multiplied by Transform.scale.
    // World angle 0 is an axis-aligned box. Any other world Z angle orients it.
    glm::vec3 size{1.f, 1.f, 1.f};
    uint32_t layer{1};           // bitmask: what this collider is
    uint32_t mask{0xFFFFFFFFu};  // bitmask: what this collider detects
    bool is_trigger{false};
    float friction{0.f};     // >= 0; negative or non-finite is 0 when solved
    float restitution{0.f};  // clamped to [0, 1] when solved; non-finite is 0
};

struct CircleCollider2D {
    float radius{0.5f};  // centered on the world position. Rotation is ignored.
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
    float friction{0.f};     // >= 0; negative or non-finite is 0 when solved
    float restitution{0.f};  // clamped to [0, 1] when solved; non-finite is 0
};

// Convex polygon in local XY, CCW, about the body origin. Not recentered. Not scaled.
// Fewer than 3 vertices, a non-finite component, or a clockwise edge produces no contact.
inline constexpr int kPolygonCollider2DCapacity = 8;

struct PolygonCollider2D {
    glm::vec2 vertices[kPolygonCollider2DCapacity]{};
    std::uint8_t count{0};
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
    float friction{0.f};     // >= 0; negative or non-finite is 0 when solved
    float restitution{0.f};  // clamped to [0, 1] when solved; non-finite is 0
};

struct CollisionEvent2D {
    ecs::Entity a{};
    ecs::Entity b{};
    CollisionPhase phase{CollisionPhase::Enter};
    bool is_trigger{false};  // true if either collider in the pair is a trigger
    glm::vec2 normal{0.f};   // unit XY from a toward b; zero on Exit
    float penetration{0.f};  // overlap along normal, >= 0; zero on Exit. Pre-solve.
    glm::vec2 point{0.f};    // one world-space XY contact point; zero on Exit

    constexpr bool operator==(const CollisionEvent2D&) const noexcept = default;
};

void run_physics_2d(ecs::World& world);

}
