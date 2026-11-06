#pragma once

// Private to the 2D physics translation units. Not installed.

#include <engine/ecs/entity.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/world.h>

#include "contact_2d.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
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

struct PhysicsOverlapState {
    std::map<std::pair<ecs::Entity, ecs::Entity>, bool> last{};  // value: was_trigger
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

void integrate_kinematic_2d(ecs::World& world, float dt);
void integrate_dynamic_2d(ecs::World& world, float dt);

[[nodiscard]] std::vector<ColliderItem2D> gather_colliders_2d(ecs::World& world);
void publish_collision_events_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders);
void solve_physics_contacts_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders);

}
