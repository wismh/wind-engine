#pragma once

#include <engine/ecs/world.h>

namespace engine::nav {

struct NavSystemsRegistered {
    bool value = false;
};

// Advances agents along waypoints and computes desired_velocity.
void run_nav_system(ecs::World& world, float delta_time);

// Registers nav systems on Schedule::Fixed / Phase::Game.
void register_nav_systems(ecs::World& world);

} // namespace engine::nav
