#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/entity.h>
#include <engine/ecs/physics.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/world.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace engine {

class JobSystem;

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
    int position_iterations{kPositionIterations};
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

// One convex part of a CompoundCollider2D: local XY about the entity origin, CCW, not recentered, not scaled. The
// same validity rules as PolygonCollider2D.
struct CompoundShape2D {
    glm::vec2 vertices[kPolygonCollider2DCapacity]{};
    std::uint8_t count{0};
};

// Many convex shapes on one entity, for chunked static terrain: one entity per chunk instead of one per cell. Static
// or under a Kinematic body. Under a Dynamic body (on the entity or above it) it is ignored and a warning is logged
// once per entity; dynamic compounds are future work. No trigger: use separate trigger entities. One material for
// every shape. Editing `shapes` in place is an in-place static edit: mark_static_colliders_dirty_2d(world, chunk).
struct CompoundCollider2D {
    std::vector<CompoundShape2D> shapes{};
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    float friction{0.f};     // >= 0; negative or non-finite is 0 when solved
    float restitution{0.f};  // clamped to [0, 1] when solved; non-finite is 0
};

// Destructible terrain: an SdfField2D (solid where the field is negative) in the entity's local XY. The entity's
// world yaw and position apply; scale is ignored, as for every collider. Static or under a Kinematic body, like
// CompoundCollider2D: under a Dynamic body it is ignored and a warning is logged once per entity. No trigger. One
// material for the whole field. Each allocated chunk is one collider part (one static cache slot and tree leaf);
// a body only touches the field through allocated chunks, so a body inside an unallocated `Solid` region gets no
// contact. Circles test their center, boxes and polygons test their vertices and points along each edge at most one
// cell apart: a field feature thinner than that spacing can slip between the samples, and a penetration deeper than
// `band` has no gradient and no contact. Field against field is no contact. Editing `field` in place (carve, fill,
// renormalize, assign_chunk) is an in-place static edit: mark_static_colliders_dirty_2d(world, entity) re-reads only
// the chunks whose revision changed and wakes sleeping bodies over them. Not replicated: replicate the edit calls.
struct SdfCollider2D {
    SdfField2D field{};
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    float friction{0.f};     // >= 0; negative or non-finite is 0 when solved
    float restitution{0.f};  // clamped to [0, 1] when solved; non-finite is 0
};

// A box as a 4-vertex CCW shape: `center`, full `size` and `angle` (radians) in the entity's local XY.
[[nodiscard]] CompoundShape2D box_shape_2d(glm::vec2 center, glm::vec2 size, float angle = 0.f);

struct CollisionEvent2D {
    ecs::Entity a{};
    ecs::Entity b{};
    CollisionPhase phase{CollisionPhase::Enter};
    bool is_trigger{false};  // true if either collider in the pair is a trigger
    glm::vec2 normal{0.f};   // unit XY from a toward b; zero on Exit
    float penetration{0.f};  // overlap along normal, >= 0; zero on Exit. Pre-solve.
    // One world-space XY contact point; zero on Exit. When the two entities touch through several shape pairs (a
    // compound, or several colliders on one entity), point, normal, penetration and is_trigger come from the deepest.
    glm::vec2 point{0.f};

    constexpr bool operator==(const CollisionEvent2D&) const noexcept = default;
};

void run_physics_2d(ecs::World& world, JobSystem* jobs = nullptr);

// A static collider has no Dynamic or Kinematic RigidBody2D on its entity or above it. run_physics_2d caches static
// colliders across ticks, so its cost does not follow their count. Adding or removing a collider, destroying an
// entity, and adding, removing or retyping a RigidBody2D are noticed on their own. Editing static geometry in place is
// not: after changing the Transform of a static collider or of any of its ancestors, its collider fields, or its
// parent, mark it so the next tick sees the change. Without a mark a background audit finds the edit within a few
// seconds and logs a warning. Geometry that moves every tick should carry a Kinematic body instead.

// The next tick re-reads the static colliders on `entity` and on all of its descendants (an ancestor's Transform moves
// the subtree) and edits only their cache slots, so an edit costs what the subtree holds, not the whole map. Mark the
// edited entity, or the ancestor whose Transform changed. Several marks in one tick are batched. A dead entity is a
// no-op; so is an entity with no static collider in its subtree (movable colliders are skipped).
void mark_static_colliders_dirty_2d(ecs::World& world, ecs::Entity entity);

// The next tick re-reads every static collider in the world. For bulk edits whose entities are not known (a loaded
// level overwriting components in place). It wins over per-entity marks pending the same tick.
void mark_static_colliders_dirty_2d(ecs::World& world);

}
