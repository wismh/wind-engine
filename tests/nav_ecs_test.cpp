#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/nav/nav_agent.h>
#include <engine/nav/nav_system.h>

namespace {

using namespace engine;
using namespace engine::nav;

TEST(NavEcs, WaypointFollowingAndDesiredVelocity) {
    ecs::World world;
    const ecs::Entity entity = world.create();
    auto& transform = world.emplace<Transform>(entity);
    transform.position = glm::vec3{0.0f, 0.0f, 0.0f};

    auto& agent = world.emplace<NavAgent>(entity);
    agent.desired_speed = 5.0f;
    agent.waypoint_reach_radius = 0.3f;
    agent.set_waypoints({{1.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}});

    EXPECT_FALSE(agent.reached_target);
    EXPECT_EQ(agent.current_waypoint_index, 0u);

    // Initial system step
    run_nav_system(world, 0.016f);
    EXPECT_NEAR(agent.desired_velocity.x, 5.0f, 1e-3f);
    EXPECT_NEAR(agent.desired_velocity.y, 0.0f, 1e-3f);
    EXPECT_NEAR(agent.desired_velocity.z, 0.0f, 1e-3f);

    // Move close to first waypoint (within 0.3f)
    transform.position = glm::vec3{0.8f, 0.0f, 0.0f};
    run_nav_system(world, 0.016f);
    EXPECT_EQ(agent.current_waypoint_index, 1u);
    EXPECT_FALSE(agent.reached_target);
    EXPECT_NEAR(agent.desired_velocity.x, 5.0f, 1e-3f);

    // Reach final waypoint
    transform.position = glm::vec3{3.0f, 0.0f, 0.0f};
    run_nav_system(world, 0.016f);
    EXPECT_TRUE(agent.reached_target);
    EXPECT_FLOAT_EQ(agent.desired_velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(agent.desired_velocity.y, 0.0f);
    EXPECT_FLOAT_EQ(agent.desired_velocity.z, 0.0f);
}

TEST(NavEcs, ScheduleRegistration) {
    ecs::World world;
    register_nav_systems(world);

    const ecs::Entity entity = world.create();
    auto& transform = world.emplace<Transform>(entity);
    transform.position = glm::vec3{0.0f, 0.0f, 0.0f};

    auto& agent = world.emplace<NavAgent>(entity);
    agent.desired_speed = 2.0f;
    agent.set_waypoints({{0.0f, 10.0f, 0.0f}});

    world.ctx<Time>().fixed_delta_time = 1.0f / 60.0f;
    world.run(ecs::Schedule::Fixed);

    EXPECT_NEAR(agent.desired_velocity.y, 2.0f, 1e-3f);
}

} // namespace
