#include "bench_3d.h"

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <string_view>
#include <utility>
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

[[nodiscard]] std::pair<int, int> count_dynamic_and_sleeping(ecs::World& world) {
    int dynamic_count = 0;
    int sleeping_count = 0;
    world.view<RigidBody3D>().each([&](const RigidBody3D& body) {
        if (body.type == BodyType::Dynamic) {
            ++dynamic_count;
            if (body.sleeping) {
                ++sleeping_count;
            }
        }
    });
    return {dynamic_count, sleeping_count};
}

[[nodiscard]] std::size_t estimate_world_memory_kb(ecs::World& world, int dynamic_bodies, int static_bodies) {
    std::size_t transform_count = 0;
    world.view<Transform>().each([&](const Transform&) { ++transform_count; });

    std::size_t box_count = 0;
    world.view<BoxCollider3D>().each([&](const BoxCollider3D&) { ++box_count; });

    std::size_t sphere_count = 0;
    world.view<SphereCollider3D>().each([&](const SphereCollider3D&) { ++sphere_count; });

    std::size_t body_count = 0;
    world.view<RigidBody3D>().each([&](const RigidBody3D&) { ++body_count; });

    const std::size_t collider_count = box_count + sphere_count;
    const std::size_t ecs_bytes = transform_count * (sizeof(Transform) + sizeof(WorldTransform) + 24u) +
                                  box_count * sizeof(BoxCollider3D) + sphere_count * sizeof(SphereCollider3D) +
                                  body_count * sizeof(RigidBody3D);
    const std::size_t broadphase_bytes = collider_count * (96u + 96u) +
                                         static_cast<std::size_t>(static_bodies) * 40u;
    const std::size_t solver_bytes = static_cast<std::size_t>(dynamic_bodies) * (112u + 3u * 176u);
    const std::size_t total_bytes = ecs_bytes + broadphase_bytes + solver_bytes;
    return std::max<std::size_t>(1u, (total_bytes + 512u) / 1024u);
}

struct StepTiming {
    double total_ms{0.0};
    double mean_us{0.0};
    double min_us{1e9};
    double max_us{0.0};
    int steps_to_sleep{-1};
};

[[nodiscard]] StepTiming measure_steps(ecs::World& world, int steps) {
    using Clock = std::chrono::high_resolution_clock;
    std::vector<double> step_times_us;
    step_times_us.reserve(static_cast<std::size_t>(steps));

    int steps_to_sleep = -1;
    for (int i = 0; i < steps; ++i) {
        const auto step_start = Clock::now();
        step_physics(world);
        const auto step_end = Clock::now();
        const double duration_us =
                std::chrono::duration<double, std::micro>(step_end - step_start).count();
        step_times_us.push_back(duration_us);

        if (steps_to_sleep < 0) {
            const auto [dyn_count, sleep_count] = count_dynamic_and_sleeping(world);
            if (dyn_count > 0 && sleep_count == dyn_count) {
                steps_to_sleep = i + 1;
            }
        }
    }

    StepTiming timing;
    timing.steps_to_sleep = steps_to_sleep;
    double sum_us = 0.0;
    for (double us : step_times_us) {
        sum_us += us;
        timing.min_us = std::min(timing.min_us, us);
        timing.max_us = std::max(timing.max_us, us);
    }
    timing.total_ms = sum_us / 1000.0;
    timing.mean_us = steps > 0 ? (sum_us / steps) : 0.0;
    return timing;
}

[[nodiscard]] bool matches_filter(std::string_view scenario_name, std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    auto to_lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    auto it = std::search(scenario_name.begin(), scenario_name.end(), filter.begin(), filter.end(),
                          [&](char a, char b) { return to_lower(a) == to_lower(b); });
    return it != scenario_name.end();
}

// 1. Vertical Stack of 10 Boxes in 3D
BenchResult bench_box_stack_10(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);
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
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);

    return BenchResult{
            .name = "3d_vertical_stack (10 boxes)",
            .steps = total_steps,
            .total_entities = kBoxes + 1,
            .dynamic_bodies = kBoxes,
            .static_bodies = 1,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, 1),
            .passed_stability = sleeping == kBoxes,
            .notes = sleeping == kBoxes ? "Sleeping converged" : "Drifting / not sleeping",
    };
}

// 2. Massive Static Voxel Grid: 20x20x3 = 1200 static cubes + 20 dynamic spheres
BenchResult bench_massive_voxel_grid(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);

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
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);
    constexpr int kStaticCount = kDimX * kDimZ * kDimY;

    return BenchResult{
            .name = "3d_voxel_grid (1200 static + 20 spheres)",
            .steps = total_steps,
            .total_entities = kStaticCount + kMovers,
            .dynamic_bodies = kMovers,
            .static_bodies = kStaticCount,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, kStaticCount),
            .passed_stability = true,
            .notes = "Tests 3D Broadphase & static-static filtering",
    };
}

// 3. 3D Sphere Funnel / Pit (80 spheres falling into an enclosed container)
BenchResult bench_sphere_funnel(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);

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
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);

    return BenchResult{
            .name = "3d_sphere_funnel (80 spheres)",
            .steps = total_steps,
            .total_entities = kSpheres + 5,
            .dynamic_bodies = kSpheres,
            .static_bodies = 5,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, 5),
            .passed_stability = true,
            .notes = "Tests 3D sphere-sphere & sphere-box contacts",
    };
}

// 4. 3D Multi-Island Stacks (8 stacks x 8 boxes = 64 dynamic boxes on shared floor)
BenchResult bench_multi_island_stacks(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{80.f, 2.f, 80.f}, 0.5f);

    constexpr int kStacks = 8;
    constexpr int kHeight = 8;
    constexpr float kBoxSize = 1.f;
    for (int s = 0; s < kStacks; ++s) {
        const float x = (static_cast<float>(s) - 3.5f) * 6.f;
        for (int i = 0; i < kHeight; ++i) {
            const float y = (kBoxSize * 0.5f) + static_cast<float>(i) * kBoxSize;
            const ecs::Entity box =
                    add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, kBoxSize}, 0.5f);
            make_dynamic(world, box, 1.f);
        }
    }

    const StepTiming timing = measure_steps(world, total_steps);
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);

    return BenchResult{
            .name = "3d_multi_island (8x8 = 64 boxes)",
            .steps = total_steps,
            .total_entities = (kStacks * kHeight) + 1,
            .dynamic_bodies = kStacks * kHeight,
            .static_bodies = 1,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, 1),
            .passed_stability = true,
            .notes = "Tests macro-level parallel 3D island solver",
    };
}

// 5. 3D Large Island Pile (5x5x5 = 125 touching dynamic boxes > 100 threshold)
BenchResult bench_large_island_pile(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{30.f, 2.f, 30.f}, 0.5f);

    constexpr int kDim = 5;
    constexpr float kBoxSize = 1.f;
    constexpr float kPitch = 0.99f;
    for (int y = 0; y < kDim; ++y) {
        for (int z = 0; z < kDim; ++z) {
            for (int x = 0; x < kDim; ++x) {
                const float px = (static_cast<float>(x) - 2.f) * kPitch;
                const float py = (kBoxSize * 0.5f) + static_cast<float>(y) * kPitch;
                const float pz = (static_cast<float>(z) - 2.f) * kPitch;
                const ecs::Entity box =
                        add_box(world, glm::vec3{px, py, pz}, glm::vec3{kBoxSize, kBoxSize, kBoxSize}, 0.5f);
                make_dynamic(world, box, 1.f);
            }
        }
    }

    const StepTiming timing = measure_steps(world, total_steps);
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);
    constexpr int kTotal = kDim * kDim * kDim;

    return BenchResult{
            .name = "3d_large_island (125 dynamic boxes)",
            .steps = total_steps,
            .total_entities = kTotal + 1,
            .dynamic_bodies = kTotal,
            .static_bodies = 1,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, 1),
            .passed_stability = true,
            .notes = "Tests micro-level graph coloring for >100 body island",
    };
}

// 6. 3D Zero-G Broadphase: 500 moving dynamic boxes
BenchResult bench_broadphase_zero_g_3d(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs, glm::vec3{0.f});

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
            .name = "3d_zero_g_broadphase (500 boxes)",
            .steps = total_steps,
            .total_entities = kCount,
            .dynamic_bodies = kCount,
            .static_bodies = 0,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = 0,
            .steps_to_sleep = -1,
            .approx_memory_kb = estimate_world_memory_kb(world, kCount, 0),
            .passed_stability = true,
            .notes = "Tests 3D AABB tree scaling",
    };
}

}

std::vector<BenchResult> run_all_3d_benchmarks(const BenchOptions& options) {
    JobSystem jobs(options.worker_threads);
    const int stack_steps = options.steps_override > 0 ? options.steps_override : 240;
    const int general_steps = options.steps_override > 0 ? options.steps_override : 180;

    std::vector<BenchResult> results;
    if (matches_filter("3d_vertical_stack (10 boxes)", options.filter)) {
        results.push_back(bench_box_stack_10(stack_steps, &jobs));
    }
    if (matches_filter("3d_voxel_grid (1200 static + 20 spheres)", options.filter)) {
        results.push_back(bench_massive_voxel_grid(general_steps, &jobs));
    }
    if (matches_filter("3d_sphere_funnel (80 spheres)", options.filter)) {
        results.push_back(bench_sphere_funnel(general_steps, &jobs));
    }
    if (matches_filter("3d_multi_island (8x8 = 64 boxes)", options.filter)) {
        results.push_back(bench_multi_island_stacks(general_steps, &jobs));
    }
    if (matches_filter("3d_large_island (125 dynamic boxes)", options.filter)) {
        results.push_back(bench_large_island_pile(general_steps, &jobs));
    }
    if (matches_filter("3d_zero_g_broadphase (500 boxes)", options.filter)) {
        results.push_back(bench_broadphase_zero_g_3d(general_steps, &jobs));
    }
    return results;
}

std::vector<BenchResult> run_all_3d_benchmarks(int steps_override, std::uint32_t worker_threads) {
    return run_all_3d_benchmarks(BenchOptions{
            .steps_override = steps_override,
            .worker_threads = worker_threads,
    });
}

}
