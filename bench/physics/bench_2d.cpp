#include "bench_2d.h"

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace engine::bench {
namespace {

void step_physics(ecs::World& world) {
    world.ctx<Time>().fixed_delta_time = kFixed;
    world.run(ecs::Schedule::Fixed);
}

[[nodiscard]] ecs::World make_physics_world(JobSystem* jobs, glm::vec3 gravity = {0.f, -60.f, 0.f}) {
    ecs::World world;
    register_engine_systems(world, EngineSystemDeps{.jobs = jobs});
    world.ctx<Physics2DConfig>().gravity = gravity;
    return world;
}

ecs::Entity add_box(ecs::World& world, glm::vec3 position, glm::vec3 size, float friction = 0.5f,
                    float restitution = 0.f) {
    const ecs::Entity entity = world.create();
    world.emplace<Transform>(entity, Transform{.position = position});
    world.emplace<BoxCollider2D>(entity, BoxCollider2D{
                                                 .size = size,
                                                 .friction = friction,
                                                 .restitution = restitution,
                                         });
    return entity;
}

void make_dynamic(ecs::World& world, ecs::Entity entity, float mass = 1.f, glm::vec3 velocity = glm::vec3{0.f}) {
    world.emplace<RigidBody2D>(entity, RigidBody2D{
                                               .velocity = velocity,
                                               .type = BodyType::Dynamic,
                                               .mass = mass,
                                       });
}

struct StepTiming {
    double total_ms{0.0};
    double mean_us{0.0};
    double min_us{1e9};
    double max_us{0.0};
};

[[nodiscard]] StepTiming measure_steps(ecs::World& world, int steps) {
    using Clock = std::chrono::high_resolution_clock;
    std::vector<double> step_times_us;
    step_times_us.reserve(static_cast<std::size_t>(steps));

    const auto t_start = Clock::now();
    for (int i = 0; i < steps; ++i) {
        const auto step_start = Clock::now();
        step_physics(world);
        const auto step_end = Clock::now();
        const double duration_us =
                std::chrono::duration<double, std::micro>(step_end - step_start).count();
        step_times_us.push_back(duration_us);
    }
    const auto t_end = Clock::now();

    StepTiming timing;
    timing.total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    double sum_us = 0.0;
    for (double us : step_times_us) {
        sum_us += us;
        timing.min_us = std::min(timing.min_us, us);
        timing.max_us = std::max(timing.max_us, us);
    }
    timing.mean_us = steps > 0 ? (sum_us / steps) : 0.0;
    return timing;
}

[[nodiscard]] int count_sleeping(ecs::World& world) {
    int count = 0;
    world.view<RigidBody2D>().each([&](const RigidBody2D& body) {
        if (body.type == BodyType::Dynamic && body.sleeping) {
            ++count;
        }
    });
    return count;
}

// 1. Box2D Classic Pyramid (10 rows = 55 boxes)
BenchResult bench_pyramid_10(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{40.f, 2.f, 1.f}, 0.5f);

    constexpr int kRows = 10;
    constexpr float kBoxSize = 1.f;
    constexpr float kSpacing = 1.02f;

    std::vector<ecs::Entity> boxes;
    ecs::Entity apex{};

    for (int row = 0; row < kRows; ++row) {
        const int count = kRows - row;
        const float y = (kBoxSize * 0.5f) + static_cast<float>(row) * kBoxSize;
        const float x_start = -0.5f * static_cast<float>(count - 1) * kSpacing;
        for (int col = 0; col < count; ++col) {
            const float x = x_start + static_cast<float>(col) * kSpacing;
            const ecs::Entity box = add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, 1.f}, 0.5f);
            make_dynamic(world, box, 1.f);
            boxes.push_back(box);
            if (row == kRows - 1) {
                apex = box;
            }
        }
    }

    const StepTiming timing = measure_steps(world, total_steps);
    const int sleeping = count_sleeping(world);
    const glm::vec3 apex_pos = ecs::world_position(world, apex);
    const float expected_apex_y = (kBoxSize * 0.5f) + static_cast<float>(kRows - 1) * kBoxSize;
    const bool stable = (apex_pos.y > expected_apex_y - 1.5f) && (sleeping == static_cast<int>(boxes.size()));

    return BenchResult{
            .name = "2D Pyramid 10 rows (55 boxes)",
            .steps = total_steps,
            .total_entities = static_cast<int>(boxes.size()) + 1,
            .dynamic_bodies = static_cast<int>(boxes.size()),
            .static_bodies = 1,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .passed_stability = stable,
            .notes = stable ? "Stable & sleeping" : "Failed sleep/drift convergence",
    };
}

// 2. Box2D Classic Pyramid 20 rows (210 boxes)
BenchResult bench_pyramid_20(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);
    world.ctx<Physics2DConfig>().position_iterations = 16;
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{80.f, 2.f, 1.f}, 0.5f);

    constexpr int kRows = 20;
    constexpr float kBoxSize = 1.f;
    constexpr float kSpacing = 1.02f;

    std::vector<ecs::Entity> boxes;
    ecs::Entity apex{};

    for (int row = 0; row < kRows; ++row) {
        const int count = kRows - row;
        const float y = (kBoxSize * 0.5f) + static_cast<float>(row) * kBoxSize;
        const float x_start = -0.5f * static_cast<float>(count - 1) * kSpacing;
        for (int col = 0; col < count; ++col) {
            const float x = x_start + static_cast<float>(col) * kSpacing;
            const ecs::Entity box = add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, 1.f}, 0.5f);
            make_dynamic(world, box, 1.f);
            boxes.push_back(box);
            if (row == kRows - 1) {
                apex = box;
            }
        }
    }

    const StepTiming timing = measure_steps(world, total_steps);
    const int sleeping = count_sleeping(world);
    const glm::vec3 apex_pos = ecs::world_position(world, apex);
    const float expected_apex_y = (kBoxSize * 0.5f) + static_cast<float>(kRows - 1) * kBoxSize;
    const bool stable = (apex_pos.y > expected_apex_y - 2.5f) && (sleeping == static_cast<int>(boxes.size()));

    return BenchResult{
            .name = "2D Pyramid 20 rows (210 boxes)",
            .steps = total_steps,
            .total_entities = static_cast<int>(boxes.size()) + 1,
            .dynamic_bodies = static_cast<int>(boxes.size()),
            .static_bodies = 1,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .passed_stability = stable,
            .notes = stable ? "Stable & sleeping" : "Failed sleep/drift convergence",
    };
}

// 3. Massive Static Tilemap with Movers: 50x50 = 2500 static blocks + 20 dynamic boxes
BenchResult bench_massive_tilemap(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);

    constexpr int kGridW = 50;
    constexpr int kGridH = 50;
    constexpr float kTileSize = 1.f;

    for (int y = 0; y < kGridH; ++y) {
        for (int x = 0; x < kGridW; ++x) {
            add_box(world, glm::vec3{static_cast<float>(x) * kTileSize, static_cast<float>(y) * kTileSize, 0.f},
                    glm::vec3{kTileSize, kTileSize, 1.f});
        }
    }

    constexpr int kDynamicCount = 20;
    for (int i = 0; i < kDynamicCount; ++i) {
        const float x = 2.f + static_cast<float>(i % 10) * 4.f;
        const float y = static_cast<float>(kGridH) + 2.f + static_cast<float>(i / 10) * 3.f;
        const ecs::Entity mover = add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{1.f, 1.f, 1.f});
        make_dynamic(world, mover, 1.f);
    }

    const StepTiming timing = measure_steps(world, total_steps);

    return BenchResult{
            .name = "2D Massive Tilemap (2500 static + 20 dynamic)",
            .steps = total_steps,
            .total_entities = (kGridW * kGridH) + kDynamicCount,
            .dynamic_bodies = kDynamicCount,
            .static_bodies = kGridW * kGridH,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = count_sleeping(world),
            .passed_stability = true,
            .notes = "Tests Broadphase & static-static filtering overhead",
    };
}

// 4. Clustered Avalanche / Funnel (150 dynamic boxes into V-funnel)
BenchResult bench_funnel(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);

    // Left ramp, right ramp and floor
    add_box(world, glm::vec3{-10.f, 10.f, 0.f}, glm::vec3{25.f, 1.f, 1.f}, 0.3f);
    add_box(world, glm::vec3{10.f, 10.f, 0.f}, glm::vec3{25.f, 1.f, 1.f}, 0.3f);
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{10.f, 2.f, 1.f}, 0.3f);

    constexpr int kBoxes = 150;
    for (int i = 0; i < kBoxes; ++i) {
        const float x = -4.f + static_cast<float>(i % 8) * 1.1f;
        const float y = 15.f + static_cast<float>(i / 8) * 1.5f;
        const ecs::Entity box = add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{0.8f, 0.8f, 1.f}, 0.3f);
        make_dynamic(world, box, 1.f);
    }

    const StepTiming timing = measure_steps(world, total_steps);

    return BenchResult{
            .name = "2D Funnel Cluster (150 dynamic boxes)",
            .steps = total_steps,
            .total_entities = kBoxes + 3,
            .dynamic_bodies = kBoxes,
            .static_bodies = 3,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = count_sleeping(world),
            .passed_stability = true,
            .notes = "Tests dense multi-contact island throughput",
    };
}

// 5. Broadphase Zero-G: 1000 dynamic boxes moving in closed arena
BenchResult bench_broadphase_zero_g(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs, glm::vec3{0.f});

    constexpr int kCount = 1000;
    for (int i = 0; i < kCount; ++i) {
        const float x = -50.f + static_cast<float>(i % 40) * 2.5f;
        const float y = -50.f + static_cast<float>(i / 40) * 4.0f;
        const float vx = (static_cast<float>(i % 7) - 3.f) * 5.f;
        const float vy = (static_cast<float>(i % 5) - 2.f) * 5.f;
        const ecs::Entity box = add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{1.f, 1.f, 1.f});
        make_dynamic(world, box, 1.f, glm::vec3{vx, vy, 0.f});
    }

    const StepTiming timing = measure_steps(world, total_steps);

    return BenchResult{
            .name = "2D Zero-G Broadphase (1000 moving boxes)",
            .steps = total_steps,
            .total_entities = kCount,
            .dynamic_bodies = kCount,
            .static_bodies = 0,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = 0,
            .passed_stability = true,
            .notes = "Tests pure AABB tree rebuild & pair query scaling",
    };
}

}

std::vector<BenchResult> run_all_2d_benchmarks(int steps_override, std::uint32_t worker_threads) {
    JobSystem jobs(worker_threads);
    const int pyramid_steps = steps_override > 0 ? steps_override : 300;
    const int general_steps = steps_override > 0 ? steps_override : 180;

    return {
            bench_pyramid_10(pyramid_steps, &jobs),
            bench_pyramid_20(pyramid_steps, &jobs),
            bench_massive_tilemap(general_steps, &jobs),
            bench_funnel(general_steps, &jobs),
            bench_broadphase_zero_g(general_steps, &jobs),
    };
}

}
