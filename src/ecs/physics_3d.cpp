#include <engine/ecs/physics_3d.h>

#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>

#include "physics_3d_internal.h"

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

}
