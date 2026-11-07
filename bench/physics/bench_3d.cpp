#include "bench_3d.h"

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_3d.h>
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

[[nodiscard]] ecs::World make_physics_world(glm::vec3 gravity = {0.f, -60.f, 0.f}) {
    ecs::World world;
    register_engine_systems(world);
    world.ctx<Physics3DConfig>().gravity = gravity;
    return world;
}

ecs::Entity add_box(ecs::World& world, glm::vec3 position, glm::vec3 size, float friction = 0.5f,
                    float restitution = 0.f) {
    const ecs::Entity entity = world.create();
    world.emplace<Transform>(entity, Transform{.position = position});
    world.emplace<BoxCollider3D>(entity, BoxCollider3D{
                                                 .size = size,
                                                 .friction = friction,
                                                 .restitution = restitution,
                                         });
    return entity;
}

ecs::Entity add_sphere(ecs::World& world, glm::vec3 position, float radius, float friction = 0.3f,
                       float restitution = 0.1f) {
    const ecs::Entity entity = world.create();
    world.emplace<Transform>(entity, Transform{.position = position});
    world.emplace<SphereCollider3D>(entity, SphereCollider3D{
                                                    .radius = radius,
                                                    .friction = friction,
                                                    .restitution = restitution,
                                            });
    return entity;
}

void make_dynamic(ecs::World& world, ecs::Entity entity, float mass = 1.f, glm::vec3 velocity = glm::vec3{0.f}) {
    world.emplace<RigidBody3D>(entity, RigidBody3D{
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
    world.view<RigidBody3D>().each([&](const RigidBody3D& body) {
        if (body.type == BodyType::Dynamic && body.sleeping) {
            ++count;
        }
    });
    return count;
}

// 1. Vertical Stack of 10 Boxes in 3D
BenchResult bench_box_stack_10(int total_steps) {
    ecs::World world = make_physics_world();
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{10.f, 2.f, 10.f}, 0.5f);

    constexpr int kBoxes = 10;
    constexpr float kBoxSize = 1.f;
    std::vector<ecs::Entity> stack;

    for (int i = 0; i < kBoxes; ++i) {
        const float y = (kBoxSize * 0.5f) + static_cast<float>(i) * kBoxSize;
        const ecs::Entity box =
                add_box(world, glm::vec3{0.f, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, kBoxSize}, 0.5f);
        make_dynamic(world, box, 1.f);
        stack.push_back(box);
    }

    const StepTiming timing = measure_steps(world, total_steps);
    const int sleeping = count_sleeping(world);

    return BenchResult{
            .name = "3D Vertical Stack (10 boxes)",
            .steps = total_steps,
            .total_entities = kBoxes + 1,
            .dynamic_bodies = kBoxes,
            .static_bodies = 1,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .passed_stability = sleeping == kBoxes,
            .notes = sleeping == kBoxes ? "Sleeping converged" : "Drifting / not sleeping",
    };
}

// 2. Massive Static Voxel Grid: 20x20x3 = 1200 static cubes + 20 dynamic spheres
BenchResult bench_massive_voxel_grid(int total_steps) {
    ecs::World world = make_physics_world();

    constexpr int kDimX = 20;
    constexpr int kDimZ = 20;
    constexpr int kDimY = 3;
    constexpr float kSize = 1.f;

    for (int y = 0; y < kDimY; ++y) {
        for (int z = 0; z < kDimZ; ++z) {
            for (int x = 0; x < kDimX; ++x) {
                add_box(world,
                        glm::vec3{static_cast<float>(x) * kSize, static_cast<float>(y) * kSize,
                                  static_cast<float>(z) * kSize},
                        glm::vec3{kSize, kSize, kSize});
            }
        }
    }

    constexpr int kMovers = 20;
    for (int i = 0; i < kMovers; ++i) {
        const float x = 2.f + static_cast<float>(i % 5) * 3.f;
        const float z = 2.f + static_cast<float>(i / 5) * 3.f;
        const float y = static_cast<float>(kDimY) * kSize + 2.f;
        const ecs::Entity sphere = add_sphere(world, glm::vec3{x, y, z}, 0.5f);
        make_dynamic(world, sphere, 1.f);
    }

    const StepTiming timing = measure_steps(world, total_steps);

    return BenchResult{
            .name = "3D Massive Voxel Grid (1200 static + 20 spheres)",
            .steps = total_steps,
            .total_entities = (kDimX * kDimZ * kDimY) + kMovers,
            .dynamic_bodies = kMovers,
            .static_bodies = kDimX * kDimZ * kDimY,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = count_sleeping(world),
            .passed_stability = true,
            .notes = "Tests 3D Broadphase & static-static filtering",
    };
}

// 3. 3D Sphere Funnel / Pit (80 spheres falling into an enclosed container)
BenchResult bench_sphere_funnel(int total_steps) {
    ecs::World world = make_physics_world();

    // 4 walls and floor
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{10.f, 2.f, 10.f}, 0.3f);
    add_box(world, glm::vec3{-5.f, 5.f, 0.f}, glm::vec3{1.f, 10.f, 10.f}, 0.3f);
    add_box(world, glm::vec3{5.f, 5.f, 0.f}, glm::vec3{1.f, 10.f, 10.f}, 0.3f);
    add_box(world, glm::vec3{0.f, 5.f, -5.f}, glm::vec3{10.f, 10.f, 1.f}, 0.3f);
    add_box(world, glm::vec3{0.f, 5.f, 5.f}, glm::vec3{10.f, 10.f, 1.f}, 0.3f);

    constexpr int kSpheres = 80;
    for (int i = 0; i < kSpheres; ++i) {
        const float x = -3.f + static_cast<float>(i % 4) * 2.f;
        const float z = -3.f + static_cast<float>((i / 4) % 4) * 2.f;
        const float y = 2.f + static_cast<float>(i / 16) * 2.f;
        const ecs::Entity sphere = add_sphere(world, glm::vec3{x, y, z}, 0.45f);
        make_dynamic(world, sphere, 1.f);
    }

    const StepTiming timing = measure_steps(world, total_steps);

    return BenchResult{
            .name = "3D Sphere Funnel (80 dynamic spheres)",
            .steps = total_steps,
            .total_entities = kSpheres + 5,
            .dynamic_bodies = kSpheres,
            .static_bodies = 5,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = count_sleeping(world),
            .passed_stability = true,
            .notes = "Tests 3D sphere-sphere & sphere-box contacts",
    };
}

// 4. 3D Zero-G Broadphase: 500 moving dynamic boxes
BenchResult bench_broadphase_zero_g_3d(int total_steps) {
    ecs::World world = make_physics_world(glm::vec3{0.f});

    constexpr int kCount = 500;
    for (int i = 0; i < kCount; ++i) {
        const float x = -20.f + static_cast<float>(i % 10) * 4.f;
        const float y = -20.f + static_cast<float>((i / 10) % 10) * 4.f;
        const float z = -20.f + static_cast<float>(i / 100) * 8.f;
        const float vx = (static_cast<float>(i % 7) - 3.f) * 4.f;
        const float vy = (static_cast<float>(i % 5) - 2.f) * 4.f;
        const float vz = (static_cast<float>(i % 3) - 1.f) * 4.f;
        const ecs::Entity box =
                add_box(world, glm::vec3{x, y, z}, glm::vec3{1.f, 1.f, 1.f});
        make_dynamic(world, box, 1.f, glm::vec3{vx, vy, vz});
    }

    const StepTiming timing = measure_steps(world, total_steps);

    return BenchResult{
            .name = "3D Zero-G Broadphase (500 moving boxes)",
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
            .notes = "Tests 3D AABB tree scaling",
    };
}

}

std::vector<BenchResult> run_all_3d_benchmarks(int steps_override) {
    const int stack_steps = steps_override > 0 ? steps_override : 240;
    const int general_steps = steps_override > 0 ? steps_override : 180;

    return {
            bench_box_stack_10(stack_steps),
            bench_massive_voxel_grid(general_steps),
            bench_sphere_funnel(general_steps),
            bench_broadphase_zero_g_3d(general_steps),
    };
}

}
