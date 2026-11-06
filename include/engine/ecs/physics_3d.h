#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/entity.h>
#include <engine/ecs/physics.h>
#include <engine/ecs/world.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace engine {

struct RigidBody3D {
    glm::vec3 velocity{0.f};          // Kinematic: parent space. Dynamic: world space.
    glm::vec3 angular_velocity{0.f};  // radians/sec about world axes. Kinematic and static are not integrated
                                      // and are not written by the solver.
    BodyType type{BodyType::Kinematic};
    float mass{1.f};  // Dynamic only. Non-positive or non-finite uses kMinDynamicMass.
    float gravity_scale{1.f};
    // Dynamic only. Kinematic and static ignore it. A missing serialized key loads false.
    bool sleeping{false};
};

// Not a component. world.ctx<Physics3DConfig>() default-constructs it (gravity zero) on first use.
// Not serialized.
struct Physics3DConfig {
    glm::vec3 gravity{0.f};  // world space
};

struct BoxCollider3D {
    // Full extents in the box's local axes. Not multiplied by Transform.scale.
    // World rotation orients it. An identity quaternion is an axis-aligned box.
    glm::vec3 size{1.f};
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
    float friction{0.f};     // >= 0; negative or non-finite is 0 when solved
    float restitution{0.f};  // clamped to [0, 1] when solved; non-finite is 0
};

struct SphereCollider3D {
    float radius{0.5f};  // centered on the world position. Rotation is ignored.
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
    float friction{0.f};
    float restitution{0.f};
};

// Capsule along local Y. `height` is the distance between the two sphere centers.
// The total length is height + 2 * radius. Not multiplied by Transform.scale.
struct CapsuleCollider3D {
    float radius{0.5f};
    float height{1.f};
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
    float friction{0.f};
    float restitution{0.f};
};

// One world point: the deepest sample. v1 manifolds store that single point.
struct CollisionEvent3D {
    ecs::Entity a{};
    ecs::Entity b{};
    CollisionPhase phase{CollisionPhase::Enter};
    bool is_trigger{false};  // true if either collider in the pair is a trigger
    glm::vec3 normal{0.f};   // unit vector from a toward b; zero on Exit
    float penetration{0.f};  // overlap along normal, >= 0; zero on Exit. Pre-solve.
    glm::vec3 point{0.f};    // one world-space contact point; zero on Exit

    constexpr bool operator==(const CollisionEvent3D&) const noexcept = default;
};

void run_physics_3d(ecs::World& world);

}
