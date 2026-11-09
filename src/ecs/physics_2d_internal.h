#pragma once

// Private to the 2D physics translation units. Not installed.

#include <engine/ecs/entity.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include "broadphase.h"
#include "contact_2d.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace engine {

// Compound is one part of a CompoundCollider2D; it is a polygon everywhere a shape is read.
enum class ColliderShape2D : std::uint8_t { Box, Circle, Polygon, Compound };

struct ColliderItem2D {
    ecs::Entity entity{};
    glm::vec3 position{0.f};
    float angle{0.f};
    ColliderShape2D shape{ColliderShape2D::Box};
    glm::vec3 box_size{1.f, 1.f, 1.f};
    float radius{0.5f};
    glm::vec2 polygon[kPolygonCollider2DCapacity]{};
    int polygon_count{0};
    std::uint32_t layer{1};
    std::uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
    float friction{0.f};
    float restitution{0.f};
    // The nearest RigidBody2D on the entity or above it is Dynamic or Kinematic. Set at gather.
    bool movable{false};
    // Index into CompoundCollider2D::shapes; 0 for every other collider.
    std::uint32_t part{0};
};

// Gather order and the per-tick collider order: shape (boxes, circles, polygons, compound parts), entity, part.
[[nodiscard]] inline bool collider_order_less_2d(const ColliderItem2D& a, const ColliderItem2D& b) {
    if (a.shape != b.shape) {
        return static_cast<int>(a.shape) < static_cast<int>(b.shape);
    }
    if (a.entity != b.entity) {
        return a.entity < b.entity;
    }
    return a.part < b.part;
}

struct OverlapRecord2D {
    std::pair<ecs::Entity, ecs::Entity> pair{};
    bool is_trigger{false};
    int event{-1};  // index into PhysicsOverlapState::pending while publishing; not meaningful afterwards

    [[nodiscard]] constexpr friend bool operator<(const OverlapRecord2D& lhs, const OverlapRecord2D& rhs) noexcept {
        return lhs.pair < rhs.pair;
    }

    [[nodiscard]] constexpr friend bool operator==(const OverlapRecord2D& lhs, const OverlapRecord2D& rhs) noexcept {
        return lhs.pair == rhs.pair;
    }
};

struct PhysicsOverlapState {
    std::vector<OverlapRecord2D> last{};
    std::vector<OverlapRecord2D> current{};
    std::vector<CollisionEvent2D> pending{};  // scratch: this tick's Enter / Stay, deepest shape pair per entity pair
};

// Static collider cache (docs/tech/modules/ECS.md, Physics). A static collider is one with no movable body on its
// entity or above it. Its item and AABB are kept here across ticks; the cache is re-read only when it is dirty, and
// then the static tree is edited per changed collider instead of rebuilt.
struct PhysicsBroadphase2D {
    struct Slot {
        ColliderItem2D item{};
        std::uint32_t compound_size{0};  // CompoundCollider2D::shapes.size() when the slot was read; audited
        bool live{false};
    };
    struct Key {
        ecs::Entity entity{};
        ColliderShape2D shape{ColliderShape2D::Box};
        std::uint32_t part{0};
        int slot{-1};
    };

    Broadphase static_tree{};
    Broadphase dynamic_tree{};
    std::vector<Slot> static_slots{};               // solid statics; slot i is leaf i of static_tree
    std::vector<int> free_slots{};
    std::vector<Key> static_keys{};                 // live slots sorted by (entity, shape, part)
    std::vector<ColliderItem2D> static_triggers{};  // queried every tick like active colliders
    std::vector<ecs::Entity> static_entities{};     // sorted, unique: every entity cached as static
    std::vector<ecs::Entity> active_entities{};     // sorted, unique: entities with a movable collider last tick
    std::uint64_t box_version{0};
    std::uint64_t circle_version{0};
    std::uint64_t polygon_version{0};
    std::uint64_t compound_version{0};
    bool dirty{true};
    std::size_t audit_cursor{0};
    std::vector<ecs::Entity> audit_warned{};  // sorted; each entity is warned about once
    std::uint64_t rebuild_count{0};           // re-reads of the static colliders
    std::vector<ecs::Entity> dynamic_compound_warned{};  // sorted; compounds left out under a Dynamic body
};

// One key per shape pair: lower entity first, then the ColliderShape2D and part of each side. Not serialized.
struct ContactCacheKey2D {
    ecs::Entity a{};
    ecs::Entity b{};
    std::uint8_t shape_a{0};
    std::uint8_t shape_b{0};
    std::uint32_t part_a{0};
    std::uint32_t part_b{0};

    bool operator<(const ContactCacheKey2D& other) const {
        if (a != other.a) {
            return a < other.a;
        }
        if (b != other.b) {
            return b < other.b;
        }
        if (shape_a != other.shape_a) {
            return shape_a < other.shape_a;
        }
        if (shape_b != other.shape_b) {
            return shape_b < other.shape_b;
        }
        if (part_a != other.part_a) {
            return part_a < other.part_a;
        }
        return part_b < other.part_b;
    }
};

// Two impulse pairs, one per manifold point. Seed only: not applied to velocity up front.
struct PhysicsContactCache2D {
    struct Impulse {
        float normal[kContact2DPointCapacity]{};
        float tangent[kContact2DPointCapacity]{};
        glm::vec2 normal_axis{0.f};
    };
    std::map<ContactCacheKey2D, Impulse> impulses{};
};

// Quiet-step counter. Not a component and not serialized.
struct PhysicsSleepState2D {
    std::map<ecs::Entity, int> quiet{};
};

[[nodiscard]] bool layers_match_2d(const ColliderItem2D& a, const ColliderItem2D& b);
[[nodiscard]] ContactShape2D contact_shape_of_2d(const ColliderItem2D& item);
[[nodiscard]] Aabb aabb_of_2d(const ColliderItem2D& item);

void wake_sleepers_by_velocity_2d(ecs::World& world);
// `active` is this tick's gather_active_colliders_2d.
void wake_sleepers_by_overlap_2d(ecs::World& world, std::span<const ColliderItem2D> active);
void update_sleep_2d(ecs::World& world, std::span<const ecs::Entity> contacting);

void integrate_kinematic_2d(ecs::World& world, float dt);
void integrate_dynamic_2d(ecs::World& world, float dt);

// WorldTransform when the entity has one, else model_matrix(transform): ecs::world_matrix without the Transform lookup.
[[nodiscard]] glm::mat4 collider_world_matrix_2d(const ecs::World& world, ecs::Entity entity,
                                                 const Transform& transform);
// Items at the pose of `matrix`: position is its translation, angle the heading of its X axis in XY.
[[nodiscard]] ColliderItem2D make_box_item_2d(ecs::Entity entity, const glm::mat4& matrix, const BoxCollider2D& box,
                                              bool movable);
[[nodiscard]] ColliderItem2D make_circle_item_2d(ecs::Entity entity, const glm::mat4& matrix,
                                                 const CircleCollider2D& circle, bool movable);
[[nodiscard]] ColliderItem2D make_polygon_item_2d(ecs::Entity entity, const glm::mat4& matrix,
                                                  const PolygonCollider2D& polygon, bool movable);
// Part `part` of `compound`; the caller checks `part < shapes.size()`.
[[nodiscard]] ColliderItem2D make_compound_item_2d(ecs::Entity entity, const glm::mat4& matrix,
                                                   const CompoundCollider2D& compound, std::uint32_t part,
                                                   bool movable);

// The entity has a Transform and at least one 2D collider, CompoundCollider2D included.
[[nodiscard]] bool has_collider_2d(const ecs::World& world, ecs::Entity entity);

// Every collider of `entity` (box, circle, polygon, then each compound part when `with_compound`) at its current
// world pose. None without Transform.
void append_colliders_2d(const ecs::World& world, ecs::Entity entity, bool movable, bool with_compound,
                         std::vector<ColliderItem2D>& out);

// Colliders on Dynamic and Kinematic bodies and on their descendants (down to the next RigidBody2D), sorted by
// collider_order_less_2d. Cost follows the bodies, not the static colliders. A CompoundCollider2D under a Dynamic
// body is left out (warned once per entity). `movable_entities` receives every walked entity that has a collider,
// those left-out compounds included, sorted and unique: they are never cached as static.
[[nodiscard]] std::vector<ColliderItem2D> gather_active_colliders_2d(ecs::World& world,
                                                                     std::vector<ecs::Entity>& movable_entities);
// Re-reads position and angle of each item, after the dynamic integrate moved them.
void refresh_collider_poses_2d(const ecs::World& world, std::span<ColliderItem2D> items);

// Validates and, when dirty, rebuilds PhysicsBroadphase2D's static cache. `movable_entities` is this tick's from
// gather_active_colliders_2d.
void prepare_static_colliders_2d(ecs::World& world, std::vector<ecs::Entity> movable_entities);

// Fills `colliders` with the active items, the static triggers and the cached statics whose AABB meets one of them,
// sorted by collider_order_less_2d, and returns the candidate pairs as indices into `colliders`, sorted.
[[nodiscard]] std::vector<std::pair<int, int>> compute_broadphase_candidates_2d(
        ecs::World& world, std::span<const ColliderItem2D> active, std::vector<ColliderItem2D>& colliders);
void publish_collision_events_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders,
                                 std::span<const std::pair<int, int>> candidate_pairs);
void solve_physics_contacts_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders,
                               std::span<const std::pair<int, int>> candidate_pairs, JobSystem* jobs = nullptr);

}
