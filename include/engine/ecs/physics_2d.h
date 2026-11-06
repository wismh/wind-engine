#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace engine {

enum class BodyType2D {
    Static,     // does not integrate; inverse mass 0
    Kinematic,  // default; parent-space velocity; the solver does not change it
    Dynamic,    // world XY velocity, gravity, positional response
};

struct RigidBody2D {
    glm::vec3 velocity{0};  // Kinematic: parent space. Dynamic: world XY; z is world z.
    BodyType2D type{BodyType2D::Kinematic};
    float mass{1.f};  // Dynamic only. Non-positive or non-finite uses kMinDynamicMass.
    float gravity_scale{1.f};
};

// Not a component. world.ctx<Physics2DConfig>() default-constructs it (gravity zero) on first use.
struct Physics2DConfig {
    glm::vec3 gravity{0.f};  // world space
};

inline constexpr float kMinDynamicMass = 1.0e-4f;
inline constexpr float kContactSlop2D = 1.0e-3f;
inline constexpr int kPositionIterations2D = 8;

struct BoxCollider2D {
    glm::vec3 size{1.f, 1.f, 1.f};  // full extents; AABB centered on the world position
    uint32_t layer{1};              // bitmask: what this collider is
    uint32_t mask{0xFFFFFFFFu};     // bitmask: what this collider detects
    bool is_trigger{false};
};

struct CircleCollider2D {
    float radius{0.5f};  // centered on the world position
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
    glm::vec2 normal{0.f};   // unit XY from a toward b; zero on Exit
    float penetration{0.f};  // overlap along normal, >= 0; zero on Exit. Pre-solve.
    glm::vec2 point{0.f};    // one world-space XY contact point; zero on Exit

    constexpr bool operator==(const CollisionEvent2D&) const noexcept = default;
};

void run_physics_2d(ecs::World& world);

}
