#include "physics_2d_internal.h"
#include "physics_owner.h"
#include "physics_sleep.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine {
namespace {

[[nodiscard]] bool is_sleeping_dynamic(const RigidBody2D* body) {
    return body != nullptr && body->type == BodyType::Dynamic && body->sleeping;
}

[[nodiscard]] bool is_awake_dynamic(const RigidBody2D* body) {
    return body != nullptr && body->type == BodyType::Dynamic && !body->sleeping;
}

[[nodiscard]] bool is_moving_kinematic(const RigidBody2D* body) {
    if (body == nullptr || body->type != BodyType::Kinematic) {
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
    return query_sorted_pairs(boxes);
}

}

void wake_sleepers_by_velocity_2d(ecs::World& world) {
    world.view<RigidBody2D>().each([&](ecs::Entity entity, RigidBody2D& body) {
        if (claimed_by_physics_3d(world, entity) || body.type != BodyType::Dynamic || !body.sleeping) {
            return;
        }
        const float linear = glm::length(body.velocity);
        const float angular = std::abs(body.angular_velocity);
        if (sleep_speed_wakes(linear, angular)) {
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
        if (a.entity == b.entity || claimed_by_physics_3d(world, a.entity) ||
            claimed_by_physics_3d(world, b.entity) || !layers_match_2d(a, b)) {
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
        if (claimed_by_physics_3d(world, entity) || body.type != BodyType::Dynamic) {
            return;
        }
        const float linear = glm::length(body.velocity);
        const float angular = std::abs(body.angular_velocity);
        int count = 0;
        if (const auto found = state.quiet.find(entity); found != state.quiet.end()) {
            count = found->second;
        }
        const SleepAdvance step = advance_sleep(body.sleeping, count, linear, angular, hits.contains(entity));
        body.sleeping = step.sleeping;
        if (step.quiet > 0) {
            next.emplace(entity, step.quiet);
        }
    });
    state.quiet = std::move(next);
}

}
