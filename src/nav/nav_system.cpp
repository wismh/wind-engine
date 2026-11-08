#include <engine/nav/nav_system.h>

#include <engine/core/time.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/nav/nav_agent.h>

#include <glm/geometric.hpp>

namespace engine::nav {

void run_nav_system(ecs::World& world, float delta_time) {
    if (delta_time <= 0.0f) {
        delta_time = world.ctx<Time>().fixed_delta_time;
        if (delta_time <= 0.0f) {
            delta_time = kFixed;
        }
    }

    auto view = world.view<Transform, NavAgent>();
    for (ecs::Entity entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& agent = view.get<NavAgent>(entity);

        agent.time_since_repath += delta_time;

        if (agent.waypoints.empty() || agent.reached_target) {
            agent.desired_velocity = glm::vec3{0.0f};
            continue;
        }

        glm::vec3 target_pos = agent.waypoints[agent.current_waypoint_index];
        float dist = glm::distance(transform.position, target_pos);

        while (dist <= agent.waypoint_reach_radius && agent.current_waypoint_index + 1 < agent.waypoints.size()) {
            ++agent.current_waypoint_index;
            target_pos = agent.waypoints[agent.current_waypoint_index];
            dist = glm::distance(transform.position, target_pos);
        }

        if (dist <= agent.waypoint_reach_radius && agent.current_waypoint_index + 1 == agent.waypoints.size()) {
            agent.reached_target = true;
            agent.desired_velocity = glm::vec3{0.0f};
        } else {
            agent.reached_target = false;
            const glm::vec3 direction = (dist > 1e-4f) ? ((target_pos - transform.position) / dist) : glm::vec3{0.0f};
            agent.desired_velocity = direction * agent.desired_speed;
        }
    }
}

void register_nav_systems(ecs::World& world) {
    if (world.ctx<NavSystemsRegistered>().value) {
        return;
    }
    world.ctx<NavSystemsRegistered>().value = true;
    world.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [](ecs::World& w) {
        run_nav_system(w, 0.0f);
    });
}

} // namespace engine::nav
