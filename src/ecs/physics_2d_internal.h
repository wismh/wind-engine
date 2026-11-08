#pragma once

// Private to the 2D physics translation units. Not installed.

#include <engine/ecs/entity.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/world.h>

#include "broadphase.h"
#include "contact_2d.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace engine {

enum class ColliderShape2D : std::uint8_t { Box, Circle, Polygon };

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
};

struct OverlapRecord2D {
    std::pair<ecs::Entity, ecs::Entity> pair{};
    bool is_trigger{false};

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
};

struct StaticColliderItem2D {
    ecs::Entity entity{};
    Aabb box{};
    int collider_index{-1};
};

struct PhysicsBroadphase2D {
    Broadphase static_tree{};
    Broadphase dynamic_tree{};
    std::vector<StaticColliderItem2D> static_items{};
    bool static_dirty{true};
};

// Pair key matches the solver order: lower entity, then shape kind. Not serialized.
struct ContactCacheKey2D {
    ecs::Entity a{};
    ecs::Entity b{};
    std::uint8_t shape_a{0};
    std::uint8_t shape_b{0};

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
        return shape_b < other.shape_b;
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

[[nodiscard]] inline bool has_movable_body_2d(const ecs::World& world, ecs::Entity entity) {
    ecs::Entity current = entity;
    for (int step = 0; step < ecs::kMaxHierarchyDepth && world.valid(current); ++step) {
        if (const RigidBody2D* body = world.try_get<RigidBody2D>(current)) {
            return body->type == BodyType::Dynamic || body->type == BodyType::Kinematic;
        }
        const Parent* parent = world.try_get<Parent>(current);
        if (parent == nullptr || !world.valid(parent->entity)) {
            break;
        }
        current = parent->entity;
    }
    return false;
}

[[nodiscard]] bool layers_match_2d(const ColliderItem2D& a, const ColliderItem2D& b);
[[nodiscard]] ContactShape2D contact_shape_of_2d(const ColliderItem2D& item);
[[nodiscard]] Aabb aabb_of_2d(const ColliderItem2D& item);

void wake_sleepers_by_velocity_2d(ecs::World& world);
void wake_sleepers_by_overlap_2d(ecs::World& world);
void update_sleep_2d(ecs::World& world, std::span<const ecs::Entity> contacting);

void integrate_kinematic_2d(ecs::World& world, float dt);
void integrate_dynamic_2d(ecs::World& world, float dt);

[[nodiscard]] std::vector<ColliderItem2D> gather_colliders_2d(ecs::World& world);
[[nodiscard]] std::vector<std::pair<int, int>> compute_broadphase_candidates_2d(
        ecs::World& world, const std::vector<ColliderItem2D>& colliders);
void publish_collision_events_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders,
                                 std::span<const std::pair<int, int>> candidate_pairs);
void solve_physics_contacts_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders,
                               std::span<const std::pair<int, int>> candidate_pairs);

}
