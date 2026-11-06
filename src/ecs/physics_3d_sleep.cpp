#include "physics_3d_internal.h"
#include "physics_sleep.h"

#include <glm/geometric.hpp>

#include <unordered_set>
#include <utility>
#include <vector>

namespace engine {
namespace {

[[nodiscard]] bool is_sleeping_dynamic(const RigidBody3D* body) {
    return body != nullptr && body->type == BodyType::Dynamic && body->sleeping;
}

[[nodiscard]] bool is_awake_dynamic(const RigidBody3D* body) {
    return body != nullptr && body->type == BodyType::Dynamic && !body->sleeping;
}

[[nodiscard]] bool is_moving_kinematic(const RigidBody3D* body) {
    if (body == nullptr || body->type != BodyType::Kinematic) {
        return false;
    }
    return body->velocity != glm::vec3{0.f} || body->angular_velocity != glm::vec3{0.f};
}

void clear_sleep(ecs::World& world, ecs::Entity entity) {
    RigidBody3D* body = world.try_get<RigidBody3D>(entity);
    if (body == nullptr) {
        return;
    }
    body->sleeping = false;
    world.ctx<PhysicsSleepState3D>().quiet.erase(entity);
}

[[nodiscard]] std::vector<std::pair<int, int>> candidate_pairs(const std::vector<ColliderItem3D>& colliders) {
    std::vector<Aabb> boxes;
    boxes.reserve(colliders.size());
    for (const ColliderItem3D& item : colliders) {
        boxes.push_back(aabb_of_3d(item));
    }
    return query_sorted_pairs(boxes);
}

}

void wake_sleepers_by_velocity_3d(ecs::World& world) {
    world.view<RigidBody3D>().each([&](ecs::Entity entity, RigidBody3D& body) {
        if (body.type != BodyType::Dynamic || !body.sleeping) {
            return;
        }
        const float linear = glm::length(body.velocity);
        const float angular = glm::length(body.angular_velocity);
        if (sleep_speed_wakes(linear, angular)) {
            body.sleeping = false;
            world.ctx<PhysicsSleepState3D>().quiet.erase(entity);
        }
    });
}

void wake_sleepers_by_overlap_3d(ecs::World& world) {
    const std::vector<ColliderItem3D> colliders = gather_colliders_3d(world);
    std::vector<ecs::Entity> wake;
    for (const auto& [index_a, index_b] : candidate_pairs(colliders)) {
        const ColliderItem3D& a = colliders[static_cast<std::size_t>(index_a)];
        const ColliderItem3D& b = colliders[static_cast<std::size_t>(index_b)];
        if (a.entity == b.entity || !layers_match_3d(a, b)) {
            continue;
        }
        const RigidBody3D* body_a = world.try_get<RigidBody3D>(a.entity);
        const RigidBody3D* body_b = world.try_get<RigidBody3D>(b.entity);
        const bool wake_a = is_sleeping_dynamic(body_a) && (is_awake_dynamic(body_b) || is_moving_kinematic(body_b));
        const bool wake_b = is_sleeping_dynamic(body_b) && (is_awake_dynamic(body_a) || is_moving_kinematic(body_a));
        if (!wake_a && !wake_b) {
            continue;
        }
        const ContactManifold3D manifold = make_contact_3d(contact_shape_of_3d(a), contact_shape_of_3d(b));
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

void update_sleep_3d(ecs::World& world, std::span<const ecs::Entity> contacting) {
    PhysicsSleepState3D& state = world.ctx<PhysicsSleepState3D>();
    const std::unordered_set<ecs::Entity> hits(contacting.begin(), contacting.end());
    std::map<ecs::Entity, int> next;
    world.view<RigidBody3D>().each([&](ecs::Entity entity, RigidBody3D& body) {
        if (body.type != BodyType::Dynamic) {
            return;
        }
        const float linear = glm::length(body.velocity);
        const float angular = glm::length(body.angular_velocity);
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
