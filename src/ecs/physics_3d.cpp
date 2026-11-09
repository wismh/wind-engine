#include <engine/ecs/physics_3d.h>

#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>

#include "core/state_hash.h"
#include "physics_3d_internal.h"

#include <engine/ecs/transform.h>

#include <algorithm>
#include <vector>

namespace engine {

void run_physics_3d(ecs::World& world, JobSystem* jobs) {
    const float dt = world.ctx<Time>().fixed_delta_time;

    wake_sleepers_by_velocity_3d(world);
    integrate_kinematic_3d(world, dt);
    ecs::update_world_transforms(world);

    wake_sleepers_by_overlap_3d(world);
    integrate_dynamic_3d(world, dt);
    ecs::update_world_transforms(world);

    // Events use this pose (pre-solve). The solver runs after publish and must not change their order.
    const std::vector<ColliderItem3D> colliders = gather_colliders_3d(world);
    publish_collision_events_3d(world, colliders);
    solve_physics_contacts_3d(world, colliders, jobs);
    ecs::update_world_transforms(world);
}

namespace {

void hash_entity(StateHasher& hasher, ecs::Entity entity) {
    hasher.u32(entity.index);
    hasher.u32(entity.generation);
}

}

std::uint64_t physics_state_hash_3d(ecs::World& world) {
    std::vector<ecs::Entity> bodies;
    for (const ecs::Entity entity : world.view<RigidBody3D>()) {
        bodies.push_back(entity);
    }
    std::sort(bodies.begin(), bodies.end());

    StateHasher hasher;
    hasher.u64(bodies.size());
    for (const ecs::Entity entity : bodies) {
        const RigidBody3D& body = world.get<RigidBody3D>(entity);
        hash_entity(hasher, entity);
        hasher.u8(static_cast<std::uint8_t>(body.type));
        if (const Transform* transform = world.try_get<Transform>(entity)) {
            hasher.u8(1);
            hasher.vec3(transform->position);
            hasher.quat(transform->rotation);
        } else {
            hasher.u8(0);
        }
        hasher.vec3(body.velocity);
        hasher.vec3(body.angular_velocity);
        hasher.u8(body.sleeping ? 1 : 0);
    }

    const PhysicsContactCache3D* cache = world.try_ctx<PhysicsContactCache3D>();
    hasher.u64(cache != nullptr ? cache->impulses.size() : 0);
    if (cache != nullptr) {
        for (const auto& [key, impulse] : cache->impulses) {
            hash_entity(hasher, key.a);
            hash_entity(hasher, key.b);
        hasher.u8(key.shape_a);
        hasher.u8(key.shape_b);
        hasher.f32(impulse.normal);
        hasher.f32(impulse.tangent[0]);
        hasher.f32(impulse.tangent[1]);
        hasher.vec3(impulse.normal_axis);
        }
    }

    const PhysicsSleepState3D* sleep = world.try_ctx<PhysicsSleepState3D>();
    hasher.u64(sleep != nullptr ? sleep->quiet.size() : 0);
    if (sleep != nullptr) {
        for (const auto& [entity, quiet] : sleep->quiet) {
            hash_entity(hasher, entity);
            hasher.i32(quiet);
        }
    }
    return hasher.value();
}

}
