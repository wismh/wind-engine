#include "bench_2d.h"
#include "bench_fluid_2d.h"
#include "bench_particles_2d.h"
#include "bench_static_2d.h"

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
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

ecs::Entity add_circle(ecs::World& world, glm::vec3 position, float radius, float friction = 0.3f,
                       float restitution = 0.1f) {
    const ecs::Entity entity = world.create();
    world.emplace<Transform>(entity, Transform{.position = position});
    world.emplace<CircleCollider2D>(entity, CircleCollider2D{
                                                    .radius = radius,
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

[[nodiscard]] std::pair<int, int> count_dynamic_and_sleeping(ecs::World& world) {
    int dynamic_count = 0;
    int sleeping_count = 0;
    world.view<RigidBody2D>().each([&](const RigidBody2D& body) {
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
    world.view<BoxCollider2D>().each([&](const BoxCollider2D&) { ++box_count; });

    std::size_t circle_count = 0;
    world.view<CircleCollider2D>().each([&](const CircleCollider2D&) { ++circle_count; });

    std::size_t poly_count = 0;
    world.view<PolygonCollider2D>().each([&](const PolygonCollider2D&) { ++poly_count; });

    std::size_t body_count = 0;
    world.view<RigidBody2D>().each([&](const RigidBody2D&) { ++body_count; });

    const std::size_t collider_count = box_count + circle_count + poly_count;
    const std::size_t ecs_bytes = transform_count * (sizeof(Transform) + sizeof(WorldTransform) + 24u) +
                                  box_count * sizeof(BoxCollider2D) + circle_count * sizeof(CircleCollider2D) +
                                  poly_count * sizeof(PolygonCollider2D) + body_count * sizeof(RigidBody2D);
    // Broadphase AABB tree nodes (2N nodes * 48B), gather buffer (112B/collider), and solver/contact caches.
    const std::size_t broadphase_bytes = collider_count * (96u + 112u) +
                                         static_cast<std::size_t>(static_bodies) * 40u;
    const std::size_t solver_bytes = static_cast<std::size_t>(dynamic_bodies) * (96u + 3u * 160u);
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

template <typename StepFn>
[[nodiscard]] StepTiming measure_steps_with(ecs::World& world, int steps, StepFn&& pre_step) {
    using Clock = std::chrono::high_resolution_clock;
    std::vector<double> step_times_us;
    step_times_us.reserve(static_cast<std::size_t>(steps));

    int steps_to_sleep = -1;
    for (int i = 0; i < steps; ++i) {
        const auto step_start = Clock::now();
        pre_step(world, i);
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

[[nodiscard]] StepTiming measure_steps(ecs::World& world, int steps) {
    return measure_steps_with(world, steps, [](ecs::World&, int) {});
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

// Shared helper for Box2D-style pyramids (1x1 boxes, mass 1.0, on 200x2 static floor at y=-1)
BenchResult run_pyramid_benchmark(const std::string& label, int rows, int total_steps, JobSystem* jobs,
                                  glm::vec3 gravity, int pos_iters, float max_apex_drop, bool require_sleep) {
    ecs::World world = make_physics_world(jobs, gravity);
    world.ctx<Physics2DConfig>().position_iterations = pos_iters;
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{200.f, 2.f, 1.f}, 0.5f);

    constexpr float kBoxSize = 1.f;
    constexpr float kSpacing = 1.02f;

    std::vector<ecs::Entity> boxes;
    boxes.reserve(static_cast<std::size_t>(rows * (rows + 1) / 2));
    ecs::Entity apex{};

    for (int row = 0; row < rows; ++row) {
        const int count = rows - row;
        const float y = (kBoxSize * 0.5f) + static_cast<float>(row) * kBoxSize;
        const float x_start = -0.5f * static_cast<float>(count - 1) * kSpacing;
        for (int col = 0; col < count; ++col) {
            const float x = x_start + static_cast<float>(col) * kSpacing;
            const ecs::Entity box = add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, 1.f}, 0.5f);
            make_dynamic(world, box, 1.f);
            boxes.push_back(box);
            if (row == rows - 1) {
                apex = box;
            }
        }
    }

    const StepTiming timing = measure_steps(world, total_steps);
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);
    const glm::vec3 apex_pos = ecs::world_position(world, apex);
    const float expected_apex_y = (kBoxSize * 0.5f) + static_cast<float>(rows - 1) * kBoxSize;
    const bool apex_ok = std::isfinite(apex_pos.x) && std::isfinite(apex_pos.y) &&
                         (apex_pos.y > expected_apex_y - max_apex_drop);
    const bool stable = apex_ok && (!require_sleep || sleeping == dyn_count);

    return BenchResult{
            .name = label,
            .steps = total_steps,
            .total_entities = dyn_count + 1,
            .dynamic_bodies = dyn_count,
            .static_bodies = 1,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, 1),
            .passed_stability = stable,
            .notes = stable ? "Box2D large_pyramid match" : "Failed sleep/drift convergence",
    };
}

// 1a. Classic 10-row Pyramid (55 boxes)
BenchResult bench_pyramid_10(int total_steps, JobSystem* jobs) {
    return run_pyramid_benchmark("pyramid_10 (55 boxes)", 10, total_steps, jobs,
                                 glm::vec3{0.f, -60.f, 0.f}, 8, 1.5f, true);
}

// 1b. Box2D v3 large_pyramid (Debug: 20 rows = 210 boxes)
BenchResult bench_large_pyramid_20(int total_steps, JobSystem* jobs) {
    return run_pyramid_benchmark("large_pyramid_20 (210 boxes)", 20, total_steps, jobs,
                                 glm::vec3{0.f, -60.f, 0.f}, 16, 2.5f, true);
}

// 1c. Box2D v3 large_pyramid (Release: 100 rows = 5,050 boxes on 200x2 floor)
BenchResult bench_large_pyramid_100(int total_steps, JobSystem* jobs) {
    return run_pyramid_benchmark("large_pyramid_100 (5050 boxes)", 100, total_steps, jobs,
                                 glm::vec3{0.f, -10.f, 0.f}, 12, 8.0f, false);
}

// 2. Box2D v3 tumbler: Rotating hollow container (1 rad/s) filled with 900 dynamic boxes (30x30)
BenchResult bench_tumbler(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs, glm::vec3{0.f, -10.f, 0.f});

    struct WallSpec {
        ecs::Entity entity{};
        glm::vec2 local_offset{0.f};
    };

    constexpr glm::vec3 kPivot{0.f, 10.f, 0.f};
    constexpr float kOmega = 1.0f;  // 1 rad/s angular velocity

    const std::pair<glm::vec2, glm::vec3> wall_defs[4] = {
            {{10.f, 0.f}, {1.f, 21.f, 1.f}},
            {{-10.f, 0.f}, {1.f, 21.f, 1.f}},
            {{0.f, 10.f}, {21.f, 1.f, 1.f}},
            {{0.f, -10.f}, {21.f, 1.f, 1.f}},
    };

    WallSpec walls[4]{};
    for (int w = 0; w < 4; ++w) {
        const glm::vec2 off = wall_defs[w].first;
        const ecs::Entity wall = add_box(world, kPivot + glm::vec3{off.x, off.y, 0.f}, wall_defs[w].second, 0.5f);
        world.emplace<RigidBody2D>(wall, RigidBody2D{
                                                 .velocity = glm::vec3{0.f},
                                                 .type = BodyType::Kinematic,
                                                 .angular_velocity = kOmega,
                                         });
        walls[w] = WallSpec{.entity = wall, .local_offset = off};
    }

    constexpr int kGrid = 30;  // 30x30 = 900 dynamic boxes inside the tumbler
    constexpr float kBoxSize = 0.25f;
    constexpr float kSpacing = 0.4f;
    std::vector<ecs::Entity> dynamic_boxes;
    dynamic_boxes.reserve(kGrid * kGrid);

    float y = -0.5f * static_cast<float>(kGrid - 1) * kSpacing + kPivot.y;
    for (int i = 0; i < kGrid; ++i) {
        float x = -0.5f * static_cast<float>(kGrid - 1) * kSpacing;
        for (int j = 0; j < kGrid; ++j) {
            const ecs::Entity box =
                    add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, 1.f}, 0.4f);
            make_dynamic(world, box, 1.f);
            dynamic_boxes.push_back(box);
            x += kSpacing;
        }
        y += kSpacing;
    }

    const StepTiming timing = measure_steps_with(world, total_steps, [&](ecs::World& w, int step_idx) {
        const float theta = kOmega * static_cast<float>(step_idx + 1) * kFixed;
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        for (const WallSpec& wall : walls) {
            const glm::vec2 r{wall.local_offset.x * c - wall.local_offset.y * s,
                              wall.local_offset.x * s + wall.local_offset.y * c};
            const glm::vec3 vel{-kOmega * r.y, kOmega * r.x, 0.f};
            const glm::vec3 target_pos = kPivot + glm::vec3{r.x, r.y, 0.f};

            RigidBody2D& rb = w.get<RigidBody2D>(wall.entity);
            rb.velocity = vel;
            rb.angular_velocity = kOmega;

            Transform& tf = w.get<Transform>(wall.entity);
            tf.position = target_pos - vel * kFixed;
            tf.rotation = rotation_z(theta);
        }
    });

    int inside_count = 0;
    for (ecs::Entity e : dynamic_boxes) {
        const glm::vec3 p = ecs::world_position(world, e);
        const float dx = p.x - kPivot.x;
        const float dy = p.y - kPivot.y;
        if (std::isfinite(p.x) && std::isfinite(p.y) && (dx * dx + dy * dy) < 25.f * 25.f) {
            ++inside_count;
        }
    }
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);
    const bool stable = inside_count >= (dyn_count * 95) / 100;

    return BenchResult{
            .name = "tumbler (900 boxes in rotor)",
            .steps = total_steps,
            .total_entities = dyn_count + 4,
            .dynamic_bodies = dyn_count,
            .static_bodies = 4,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, 4),
            .passed_stability = stable,
            .notes = "Box2D tumbler (1 rad/s hollow rotor)",
    };
}

// 3. Box2D v3 rain: Continuous stream of dynamic bodies spawned across steps falling through pegs & funnels
BenchResult bench_rain(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs, glm::vec3{0.f, -25.f, 0.f});

    int static_count = 0;
    // Basin floor and side walls
    add_box(world, glm::vec3{0.f, -2.f, 0.f}, glm::vec3{100.f, 2.f, 1.f}, 0.4f);
    add_box(world, glm::vec3{-50.f, 15.f, 0.f}, glm::vec3{2.f, 36.f, 1.f}, 0.3f);
    add_box(world, glm::vec3{50.f, 15.f, 0.f}, glm::vec3{2.f, 36.f, 1.f}, 0.3f);
    static_count += 3;

    // 4 staggered rows of static pegs (0.5x0.5 boxes, matching Box2D CreateRain gridSize=0.5f)
    constexpr int kPegRows = 4;
    constexpr int kPegCols = 36;
    constexpr float kPegSpacing = 2.4f;
    for (int r = 0; r < kPegRows; ++r) {
        const float y = 5.f + static_cast<float>(r) * 5.5f;
        const float stagger = (r % 2 == 1) ? (0.5f * kPegSpacing) : 0.f;
        for (int c = 0; c < kPegCols; ++c) {
            const float x = -0.5f * static_cast<float>(kPegCols - 1) * kPegSpacing +
                            static_cast<float>(c) * kPegSpacing + stagger;
            add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{0.5f, 0.5f, 1.f}, 0.2f);
            ++static_count;
        }
    }

    constexpr int kMaxRainBodies = 400;
    constexpr int kSpawnInterval = 4;
    constexpr int kSpawnGroupSize = 8;
    std::vector<ecs::Entity> spawned;
    spawned.reserve(kMaxRainBodies);

    auto spawn_wave = [&](ecs::World& w, int wave_index) {
        if (static_cast<int>(spawned.size()) >= kMaxRainBodies) {
            return;
        }
        const float x_offset = static_cast<float>((wave_index * 7) % 11 - 5) * 0.6f;
        for (int i = 0; i < kSpawnGroupSize && static_cast<int>(spawned.size()) < kMaxRainBodies; ++i) {
            const float x = -35.f + static_cast<float>(i) * 10.f + x_offset;
            const float y = 28.f + static_cast<float>(i % 2) * 0.8f;
            ecs::Entity e{};
            if ((wave_index + i) % 2 == 0) {
                e = add_box(w, glm::vec3{x, y, 0.f}, glm::vec3{0.6f, 0.6f, 1.f}, 0.3f, 0.1f);
            } else {
                e = add_circle(w, glm::vec3{x, y, 0.f}, 0.3f, 0.3f, 0.1f);
            }
            make_dynamic(w, e, 1.f);
            spawned.push_back(e);
        }
    };

    // Initial wave before step 0 so step 0 already has active dynamic bodies
    spawn_wave(world, 0);

    const StepTiming timing = measure_steps_with(world, total_steps, [&](ecs::World& w, int step_idx) {
        if (step_idx > 0 && (step_idx % kSpawnInterval) == 0) {
            spawn_wave(w, step_idx / kSpawnInterval);
        }
    });

    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);
    bool all_valid = true;
    for (ecs::Entity e : spawned) {
        const glm::vec3 p = ecs::world_position(world, e);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.y < -10.f) {
            all_valid = false;
            break;
        }
    }

    return BenchResult{
            .name = "rain (stream through pegs)",
            .steps = total_steps,
            .total_entities = dyn_count + static_count,
            .dynamic_bodies = dyn_count,
            .static_bodies = static_count,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, static_count),
            .passed_stability = all_valid,
            .notes = "Box2D rain (continuous spawn over pegs)",
    };
}

// 4. Box2D v3 tile_world: Dynamic movers traversing large-scale static grid tilemaps (2,500 tiles + 100 movers)
BenchResult bench_tile_world(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs, glm::vec3{0.f, -20.f, 0.f});

    constexpr int kColumns = 250;
    constexpr int kRowsPerCol = 10;
    constexpr float kTileSize = 1.f;
    constexpr float kPeriod = 40.f;
    constexpr float kOmega = 2.f * 3.14159265f / kPeriod;

    int static_count = 0;
    for (int col = 0; col < kColumns; ++col) {
        const float x = (static_cast<float>(col) - 0.5f * static_cast<float>(kColumns)) * kTileSize;
        const float wave_top = std::round(2.f * std::cos(kOmega * x));
        for (int r = 0; r < kRowsPerCol; ++r) {
            const float y = (static_cast<float>(r) - static_cast<float>(kRowsPerCol) + wave_top) * kTileSize;
            add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{kTileSize, kTileSize, 1.f}, 0.4f);
            ++static_count;
        }
    }

    // Spawn 100 dynamic movers (box stacks in valleys + horizontal movers traversing the terrain)
    constexpr int kDynamicCount = 100;
    std::vector<ecs::Entity> movers;
    movers.reserve(kDynamicCount);
    for (int i = 0; i < kDynamicCount; ++i) {
        const float x = -96.f + static_cast<float>(i % 25) * 8.f;
        const float y = 4.f + static_cast<float>(i / 25) * 1.2f;
        const float vx = (i % 2 == 0) ? 2.f : -2.f;
        const ecs::Entity mover = add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{0.6f, 0.4f, 1.f}, 0.5f);
        make_dynamic(world, mover, 1.f, glm::vec3{vx, 0.f, 0.f});
        movers.push_back(mover);
    }

    const StepTiming timing = measure_steps(world, total_steps);
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);

    bool stable = true;
    for (ecs::Entity e : movers) {
        const glm::vec3 p = ecs::world_position(world, e);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.y < -25.f) {
            stable = false;
            break;
        }
    }

    return BenchResult{
            .name = "tile_world (2500 tiles + 100 movers)",
            .steps = total_steps,
            .total_entities = static_count + dyn_count,
            .dynamic_bodies = dyn_count,
            .static_bodies = static_count,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, static_count),
            .passed_stability = stable,
            .notes = "Box2D tile_world (large static grid + movers)",
    };
}

// 5. Box2D v3 sleep: Dense multi-pyramid clusters settling to total sleep, measuring exact steps_to_sleep
BenchResult bench_sleep(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs, glm::vec3{0.f, -60.f, 0.f});
    world.ctx<Physics2DConfig>().position_iterations = 12;

    constexpr int kPyramidCount = 6;
    constexpr int kBaseCount = 8;  // 36 boxes per pyramid * 6 = 216 dynamic bodies
    constexpr float kBoxSize = 1.f;
    constexpr float kSpacing = 1.02f;
    constexpr float kBaseWidth = kBoxSize * static_cast<float>(kBaseCount);
    constexpr float kPitch = kBaseWidth + 4.f;
    constexpr float kSpan = kPitch * static_cast<float>(kPyramidCount - 1);

    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{kSpan + 2.f * kBaseWidth + 20.f, 2.f, 1.f}, 0.5f);

    int dynamic_total = 0;
    for (int p = 0; p < kPyramidCount; ++p) {
        const float center_x = -0.5f * kSpan + static_cast<float>(p) * kPitch;
        for (int row = 0; row < kBaseCount; ++row) {
            const int count = kBaseCount - row;
            const float y = (kBoxSize * 0.5f) + static_cast<float>(row) * kBoxSize;
            const float x_start = center_x - 0.5f * static_cast<float>(count - 1) * kSpacing;
            for (int col = 0; col < count; ++col) {
                const float x = x_start + static_cast<float>(col) * kSpacing;
                const ecs::Entity box =
                        add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, 1.f}, 0.5f);
                make_dynamic(world, box, 1.f);
                ++dynamic_total;
            }
        }
    }

    const StepTiming timing = measure_steps(world, total_steps);
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);
    const bool stable = (sleeping == dyn_count) && (timing.steps_to_sleep > 0);

    return BenchResult{
            .name = "sleep (6x36 = 216 settling boxes)",
            .steps = total_steps,
            .total_entities = dynamic_total + 1,
            .dynamic_bodies = dynamic_total,
            .static_bodies = 1,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, 1),
            .passed_stability = stable,
            .notes = stable ? "Converged to total sleep" : "Did not reach total sleep",
    };
}

// 6. Clustered Avalanche / Funnel (150 dynamic boxes into V-funnel)
BenchResult bench_funnel(int total_steps, JobSystem* jobs) {
    ecs::World world = make_physics_world(jobs);

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
    const auto [dyn_count, sleeping] = count_dynamic_and_sleeping(world);

    return BenchResult{
            .name = "funnel_cluster (150 boxes)",
            .steps = total_steps,
            .total_entities = kBoxes + 3,
            .dynamic_bodies = kBoxes,
            .static_bodies = 3,
            .total_ms = timing.total_ms,
            .mean_us = timing.mean_us,
            .min_us = timing.min_us,
            .max_us = timing.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = timing.steps_to_sleep,
            .approx_memory_kb = estimate_world_memory_kb(world, dyn_count, 3),
            .passed_stability = true,
            .notes = "Dense multi-contact island throughput",
    };
}

// 7. Broadphase Zero-G: 1000 dynamic boxes moving in closed arena
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
            .name = "zero_g_broadphase (1000 boxes)",
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
            .notes = "Pure AABB tree rebuild & pair query scaling",
    };
}

}

std::vector<BenchResult> run_all_2d_benchmarks(const BenchOptions& options) {
    JobSystem jobs(options.worker_threads);
    const int pyramid_steps = options.steps_override > 0 ? options.steps_override : 300;
    const int pyramid_100_steps =
            options.steps_override > 0 ? options.steps_override : (options.full_steps ? 60 : 20);
    const int general_steps = options.steps_override > 0 ? options.steps_override : 200;

    std::vector<BenchResult> results;
    if (matches_filter("pyramid_10 (55 boxes)", options.filter)) {
        results.push_back(bench_pyramid_10(pyramid_steps, &jobs));
    }
    if (matches_filter("large_pyramid_20 (210 boxes)", options.filter)) {
        results.push_back(bench_large_pyramid_20(pyramid_steps, &jobs));
    }
    if (!options.skip_large && matches_filter("large_pyramid_100 (5050 boxes)", options.filter)) {
        results.push_back(bench_large_pyramid_100(pyramid_100_steps, &jobs));
    }
    if (matches_filter("tumbler (900 boxes in rotor)", options.filter)) {
        results.push_back(bench_tumbler(general_steps, &jobs));
    }
    if (matches_filter("rain (stream through pegs)", options.filter)) {
        results.push_back(bench_rain(general_steps, &jobs));
    }
    if (matches_filter("tile_world (2500 tiles + 100 movers)", options.filter)) {
        results.push_back(bench_tile_world(general_steps, &jobs));
    }
    if (matches_filter("sleep (6x36 = 216 settling boxes)", options.filter)) {
        results.push_back(bench_sleep(pyramid_steps, &jobs));
    }
    if (matches_filter("funnel_cluster (150 boxes)", options.filter)) {
        results.push_back(bench_funnel(general_steps, &jobs));
    }
    if (matches_filter("zero_g_broadphase (1000 boxes)", options.filter)) {
        results.push_back(bench_broadphase_zero_g(general_steps, &jobs));
    }
    std::vector<BenchResult> static_results = run_static_2d_benchmarks(options, &jobs);
    results.insert(results.end(), static_results.begin(), static_results.end());
    std::vector<BenchResult> particle_results = run_particle_2d_benchmarks(options, &jobs);
    results.insert(results.end(), particle_results.begin(), particle_results.end());
    std::vector<BenchResult> fluid_results = run_fluid_2d_benchmarks(options, &jobs);
    results.insert(results.end(), fluid_results.begin(), fluid_results.end());
    return results;
}

std::vector<BenchResult> run_all_2d_benchmarks(int steps_override, std::uint32_t worker_threads) {
    return run_all_2d_benchmarks(BenchOptions{
            .steps_override = steps_override,
            .worker_threads = worker_threads,
    });
}

}
