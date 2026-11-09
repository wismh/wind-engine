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

// Compound is one part of a CompoundCollider2D; it is a polygon everywhere a shape is read. Sdf is one allocated chunk
// of an SdfCollider2D: its polygon is the chunk's cell rectangle grown by 1/100 of a cell, which gives its AABB.
enum class ColliderShape2D : std::uint8_t { Box, Circle, Polygon, Compound, Sdf };

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
    // Index into CompoundCollider2D::shapes, or sdf_part_of_chunk_2d of an Sdf chunk; 0 for every other collider.
    std::uint32_t part{0};
    // Sdf only. The field read this tick: refreshed from the component whenever the item joins a tick's collider list,
    // never dereferenced from the static cache. Not compared by the cache.
    const SdfField2D* sdf{nullptr};
    // Sdf only. SdfField2D::revision of the chunk when the item was read; compared by the cache.
    std::uint64_t sdf_revision{0};
};

// SDF chunk coordinates in [-32768, 32767] encode as a part, sorted by y then x like SdfField2D::chunk_coords. Chunks
// past that range (about 1M cells from the field origin) have no collider.
inline constexpr int kSdfPartBias2D = 32768;

[[nodiscard]] inline bool sdf_chunk_has_part_2d(glm::ivec2 chunk) {
    return chunk.x >= -kSdfPartBias2D && chunk.x < kSdfPartBias2D && chunk.y >= -kSdfPartBias2D &&
           chunk.y < kSdfPartBias2D;
}

[[nodiscard]] inline std::uint32_t sdf_part_of_chunk_2d(glm::ivec2 chunk) {
    return (static_cast<std::uint32_t>(chunk.y + kSdfPartBias2D) << 16) |
           static_cast<std::uint32_t>(chunk.x + kSdfPartBias2D);
}

[[nodiscard]] inline glm::ivec2 sdf_chunk_of_part_2d(std::uint32_t part) {
    return {static_cast<int>(part & 0xFFFFu) - kSdfPartBias2D, static_cast<int>(part >> 16) - kSdfPartBias2D};
}

// Gather order and the per-tick collider order: shape (boxes, circles, polygons, compound parts, SDF chunks), entity,
// part.
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
        // When the slot was read, audited: CompoundCollider2D::shapes.size() for a compound part (a compound that
        // grew or shrank), SdfField2D::edit_count() for an SDF chunk (an edit anywhere in the field); 0 otherwise.
        std::uint64_t stamp{0};
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
    std::uint64_t sdf_version{0};
    bool dirty{true};                          // re-read every static collider next tick
    std::vector<ecs::Entity> marked_entities{};  // per-entity marks since the last tick; unsorted, may repeat
    std::size_t audit_cursor{0};
    std::vector<ecs::Entity> audit_warned{};  // sorted; each entity is warned about once
    std::uint64_t rebuild_count{0};           // re-reads of every static collider
    std::uint64_t entity_rebuild_count{0};    // re-reads of the marked entities' subtrees only
    // Collider items the last re-read made (a compound part, an SDF chunk counts one each), and the slots it added,
    // updated or dropped.
    std::size_t last_rebuild_reads{0};
    std::size_t last_rebuild_changes{0};
    // Sorted; compounds and SDF colliders left out under a Dynamic body, warned once per entity.
    std::vector<ecs::Entity> dynamic_terrain_warned{};
    // Bounds of SDF chunk slots a re-read added, changed or dropped (once the cache was built): sleeping dynamics over
    // them wake this tick (wake_sleepers_by_static_edit_2d), then the list is cleared.
    std::vector<Aabb> sdf_wake_boxes{};
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
// Sleeping dynamics in `active` whose AABB meets a PhysicsBroadphase2D::sdf_wake_boxes entry wake; the list is cleared.
void wake_sleepers_by_static_edit_2d(ecs::World& world, std::span<const ColliderItem2D> active);
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
// Chunk `chunk` of `sdf`; the caller checks that it is allocated and sdf_chunk_has_part_2d.
[[nodiscard]] ColliderItem2D make_sdf_item_2d(ecs::Entity entity, const glm::mat4& matrix, const SdfCollider2D& sdf,
                                              glm::ivec2 chunk, bool movable);
// One item per allocated chunk with a part, in chunk_coords order.
void append_sdf_items_2d(ecs::Entity entity, const glm::mat4& matrix, const SdfCollider2D& sdf, bool movable,
                         std::vector<ColliderItem2D>& out);

// The entity has a Transform and at least one 2D collider, CompoundCollider2D and SdfCollider2D included.
[[nodiscard]] bool has_collider_2d(const ecs::World& world, ecs::Entity entity);

// Every collider of `entity` (box, circle, polygon, then, when `with_terrain`, each compound part and SDF chunk) at
// its current world pose. None without Transform.
void append_colliders_2d(const ecs::World& world, ecs::Entity entity, bool movable, bool with_terrain,
                         std::vector<ColliderItem2D>& out);

// Colliders on Dynamic and Kinematic bodies and on their descendants (down to the next RigidBody2D), sorted by
// collider_order_less_2d. Cost follows the bodies, not the static colliders. A CompoundCollider2D or SdfCollider2D
// under a Dynamic body is left out (warned once per entity). `movable_entities` receives every walked entity that has
// a collider, those left out included, sorted and unique: they are never cached as static.
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
