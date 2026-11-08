#pragma once

// Private to the 3D physics translation units. Not installed.

#include <engine/ecs/entity.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/world.h>

#include "broadphase.h"
#include "contact_3d.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace engine {

enum class ColliderShape3D : std::uint8_t { Box, Sphere, Capsule };

struct ColliderItem3D {
    ecs::Entity entity{};
    glm::vec3 position{0.f};
    glm::quat rotation{1.f, 0.f, 0.f, 0.f};
    ColliderShape3D shape{ColliderShape3D::Box};
    glm::vec3 box_size{1.f};
    float radius{0.5f};
    float height{1.f};
    std::uint32_t layer{1};
    std::uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
    float friction{0.f};
    float restitution{0.f};
};

struct PhysicsOverlapState3D {
    std::map<std::pair<ecs::Entity, ecs::Entity>, bool> last{};  // value: was_trigger
};

struct ContactCacheKey3D {
    ecs::Entity a{};
    ecs::Entity b{};
    std::uint8_t shape_a{0};
    std::uint8_t shape_b{0};

    bool operator<(const ContactCacheKey3D& other) const {
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

// One point, two tangents. Seed only: not applied to velocity up front.
struct PhysicsContactCache3D {
    struct Impulse {
        float normal{0.f};
        float tangent[2]{};
        glm::vec3 normal_axis{0.f};
    };
    std::map<ContactCacheKey3D, Impulse> impulses{};
};

struct PhysicsSleepState3D {
    std::map<ecs::Entity, int> quiet{};
};

[[nodiscard]] inline bool has_movable_body_3d(const ecs::World& world, ecs::Entity entity) {
    ecs::Entity current = entity;
    for (int step = 0; step < ecs::kMaxHierarchyDepth && world.valid(current); ++step) {
        if (const RigidBody3D* body = world.try_get<RigidBody3D>(current)) {
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

[[nodiscard]] bool layers_match_3d(const ColliderItem3D& a, const ColliderItem3D& b);
[[nodiscard]] ContactShape3D contact_shape_of_3d(const ColliderItem3D& item);
[[nodiscard]] Aabb aabb_of_3d(const ColliderItem3D& item);

void wake_sleepers_by_velocity_3d(ecs::World& world);
void wake_sleepers_by_overlap_3d(ecs::World& world);
void update_sleep_3d(ecs::World& world, std::span<const ecs::Entity> contacting);

[[nodiscard]] int hierarchy_depth_3d(const ecs::World& world, ecs::Entity entity);
[[nodiscard]] glm::quat world_rotation_quat_3d(const ecs::World& world, ecs::Entity entity);

void integrate_kinematic_3d(ecs::World& world, float dt);
void integrate_dynamic_3d(ecs::World& world, float dt);

[[nodiscard]] std::vector<ColliderItem3D> gather_colliders_3d(ecs::World& world);
void publish_collision_events_3d(ecs::World& world, const std::vector<ColliderItem3D>& colliders);
void solve_physics_contacts_3d(ecs::World& world, const std::vector<ColliderItem3D>& colliders);

}
