#include <engine/ecs/physics_2d.h>

#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>

#include "physics_2d_internal.h"

namespace engine {

void run_physics_2d(ecs::World& world) {
    const float dt = world.ctx<Time>().fixed_delta_time;

    wake_sleepers_by_velocity_2d(world);
    integrate_kinematic_2d(world, dt);
    ecs::update_world_transforms(world);

    // The platform has already moved. Wake before the sleeper integrates so it is not frozen this step.
    wake_sleepers_by_overlap_2d(world);
    integrate_dynamic_2d(world, dt);
    ecs::update_world_transforms(world);

    // Events use this pose (pre-solve). The solver runs after publish and must not change their order.
    const std::vector<ColliderItem2D> colliders = gather_colliders_2d(world);
    publish_collision_events_2d(world, colliders);
    solve_physics_contacts_2d(world, colliders);
    ecs::update_world_transforms(world);
}

}
