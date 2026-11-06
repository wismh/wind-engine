#include "physics_2d_internal.h"

#include <engine/ecs/physics_3d.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine {
namespace {

[[nodiscard]] bool is_sleeping_dynamic(const RigidBody2D* body) {
    return body != nullptr && body->type == BodyType2D::Dynamic && body->sleeping;
}

[[nodiscard]] bool is_awake_dynamic(const RigidBody2D* body) {
    return body != nullptr && body->type == BodyType2D::Dynamic && !body->sleeping;
}

[[nodiscard]] bool is_moving_kinematic(const RigidBody2D* body) {
    if (body == nullptr || body->type != BodyType2D::Kinematic) {
        return false;
    }
    return body->velocity != glm::vec3{0.f} || body->angular_velocity != 0.f;
}

void clear_sleep(ecs::World& world, ecs::Entity entity) {
    RigidBody2D* body = world.try_get<RigidBody2D>(entity);
    if (body == nullptr) {
        return;
    }
    body->sleeping = false;
    world.ctx<PhysicsSleepState2D>().quiet.erase(entity);
}

[[nodiscard]] std::vector<std::pair<int, int>> candidate_pairs(const std::vector<ColliderItem2D>& colliders) {
    std::vector<Aabb> boxes;
    boxes.reserve(colliders.size());
    for (const ColliderItem2D& item : colliders) {
        boxes.push_back(aabb_of_2d(item));
    }
    Broadphase tree;
    tree.rebuild(boxes);
    std::vector<std::pair<int, int>> pairs;
    tree.query_pairs(pairs);
    std::sort(pairs.begin(), pairs.end());
    return pairs;
}

}

void wake_sleepers_by_velocity_2d(ecs::World& world) {
    world.view<RigidBody2D>().each([&](ecs::Entity entity, RigidBody2D& body) {
        if (world.try_get<RigidBody3D>(entity) != nullptr || body.type != BodyType2D::Dynamic || !body.sleeping) {
            return;
        }
        const float linear = glm::length(body.velocity);
        const float angular = std::abs(body.angular_velocity);
        if (linear >= kSleepLinear2D || angular >= kSleepAngular2D) {
            body.sleeping = false;
            world.ctx<PhysicsSleepState2D>().quiet.erase(entity);
        }
    });
}

void wake_sleepers_by_overlap_2d(ecs::World& world) {
    const std::vector<ColliderItem2D> colliders = gather_colliders_2d(world);
    std::vector<ecs::Entity> wake;
    for (const auto& [index_a, index_b] : candidate_pairs(colliders)) {
        const ColliderItem2D& a = colliders[static_cast<std::size_t>(index_a)];
        const ColliderItem2D& b = colliders[static_cast<std::size_t>(index_b)];
        if (a.entity == b.entity || world.try_get<RigidBody3D>(a.entity) != nullptr ||
            world.try_get<RigidBody3D>(b.entity) != nullptr || !layers_match_2d(a, b)) {
            continue;
        }
        const RigidBody2D* body_a = world.try_get<RigidBody2D>(a.entity);
        const RigidBody2D* body_b = world.try_get<RigidBody2D>(b.entity);
        const bool wake_a = is_sleeping_dynamic(body_a) && (is_awake_dynamic(body_b) || is_moving_kinematic(body_b));
        const bool wake_b = is_sleeping_dynamic(body_b) && (is_awake_dynamic(body_a) || is_moving_kinematic(body_a));
        if (!wake_a && !wake_b) {
            continue;
        }
        const ContactManifold2D manifold = make_contact_2d(contact_shape_of_2d(a), contact_shape_of_2d(b));
        if (manifold.count == 0) {
            continue;
        }
        if (wake_a) {
            wake.push_back(a.entity);
        }
        if (wake_b) {
            wake.push_back(b.entity);
        }
    }
    for (ecs::Entity entity : wake) {
        clear_sleep(world, entity);
    }
}

void update_sleep_2d(ecs::World& world, std::span<const ecs::Entity> contacting) {
    PhysicsSleepState2D& state = world.ctx<PhysicsSleepState2D>();
    const std::unordered_set<ecs::Entity> hits(contacting.begin(), contacting.end());
    std::map<ecs::Entity, int> next;
    world.view<RigidBody2D>().each([&](ecs::Entity entity, RigidBody2D& body) {
        if (world.try_get<RigidBody3D>(entity) != nullptr || body.type != BodyType2D::Dynamic) {
            return;
        }
        const float linear = glm::length(body.velocity);
        const float angular = std::abs(body.angular_velocity);
        const bool slow = linear < kSleepLinear2D && angular < kSleepAngular2D;
        int count = 0;
        if (const auto found = state.quiet.find(entity); found != state.quiet.end()) {
            count = found->second;
        }
        if (slow && hits.contains(entity)) {
            count = std::min(count + 1, kSleepSteps2D);
            if (count >= kSleepSteps2D) {
                body.sleeping = true;
            }
        } else {
            count = 0;
            if (linear >= kSleepLinear2D || angular >= kSleepAngular2D) {
                body.sleeping = false;
            }
        }
        if (count > 0) {
            next.emplace(entity, count);
        }
    });
    state.quiet = std::move(next);
}

}
