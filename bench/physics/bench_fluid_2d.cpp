#include "bench_fluid_2d.h"

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace engine::bench {
namespace {

using Clock = std::chrono::steady_clock;

inline constexpr float kRadius = 0.05f;

[[nodiscard]] bool matches(std::string_view name, std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    const auto it = std::search(name.begin(), name.end(), filter.begin(), filter.end(),
                                [&](char a, char b) { return lower(a) == lower(b); });
    return it != name.end();
}

// An open-top tank: walls 1 unit thick around the cavity [0, width] x [0, height].
[[nodiscard]] SdfField2D tank_field(float width, float height) {
    SdfField2D field{SdfField2DDesc{.cell_size = 0.1f, .band = 0.4f}};
    const std::array<glm::vec2, 4> outer{glm::vec2{-1.f, -1.f}, glm::vec2{width + 1.f, -1.f},
                                         glm::vec2{width + 1.f, height}, glm::vec2{-1.f, height}};
    const std::array<glm::vec2, 4> inner{glm::vec2{0.f, 0.f}, glm::vec2{width, 0.f}, glm::vec2{width, height + 2.f},
                                         glm::vec2{0.f, height + 2.f}};
    (void) field.fill_polygon(outer);
    (void) field.carve_polygon(inner);
    return field;
}

[[nodiscard]] CompoundShape2D quad(glm::vec2 lo, glm::vec2 hi) {
    CompoundShape2D shape;
    shape.count = 4;
    shape.vertices[0] = lo;
    shape.vertices[1] = {hi.x, lo.y};
    shape.vertices[2] = hi;
    shape.vertices[3] = {lo.x, hi.y};
    return shape;
}

// The tank as a CompoundCollider2D of 0.25-unit tiles (marching-squares-like terrain cells), 1 unit thick.
[[nodiscard]] CompoundCollider2D compound_tank(float width, float height) {
    CompoundCollider2D compound;
    for (float y = -1.f; y < 0.f; y += 0.25f) {
        for (float x = -1.f; x < width + 1.f; x += 0.25f) {
            compound.shapes.push_back(quad({x, y}, {x + 0.25f, y + 0.25f}));
        }
    }
    for (float y = 0.f; y < height; y += 0.25f) {
        for (float x = -1.f; x < 0.f; x += 0.25f) {
            compound.shapes.push_back(quad({x, y}, {x + 0.25f, y + 0.25f}));
            compound.shapes.push_back(quad({width + 1.f + x, y}, {width + 1.f + x + 0.25f, y + 0.25f}));
        }
    }
    return compound;
}

// Far terrain the fluid never reaches: `count` tiles in rows well below the tank.
[[nodiscard]] CompoundCollider2D far_terrain(int count) {
    CompoundCollider2D compound;
    for (int i = 0; i < count; ++i) {
        const float x = static_cast<float>(i % 200) * 0.25f;
        const float y = -50.f - static_cast<float>(i / 200) * 0.25f;
        compound.shapes.push_back(quad({x, y}, {x + 0.25f, y + 0.25f}));
    }
    return compound;
}

enum class Tank { Sdf, Compound };

// A `side` x `side` column of particles at rest spacing against the tank's left wall, released at once. With `warmup`
// ticks the column first runs that long untimed: a pool that has spread and come to rest. A compound tank also has
// 4,000 far tiles; run_physics_2d runs once first, so the fluid reads the static parts from the solver's cache.
[[nodiscard]] BenchResult bench_dam(const std::string& name, int side, int steps, int warmup, Tank tank,
                                    JobSystem* jobs) {
    const float column = static_cast<float>(side) * 2.f * kRadius;
    const float width = column * 2.5f;
    ecs::World world;
    world.ctx<Time>().fixed_delta_time = kFixed;
    world.ctx<Physics2DConfig>().gravity = {0.f, -10.f, 0.f};
    const ecs::Entity terrain = world.create();
    world.emplace<Transform>(terrain, Transform{});
    if (tank == Tank::Sdf) {
        world.emplace<SdfCollider2D>(terrain, SdfCollider2D{.field = tank_field(width, column * 1.5f)});
    } else {
        world.emplace<CompoundCollider2D>(terrain, compound_tank(width, column * 1.5f));
        const ecs::Entity far = world.create();
        world.emplace<Transform>(far, Transform{});
        world.emplace<CompoundCollider2D>(far, far_terrain(4000));
        run_physics_2d(world, jobs);
    }
    const ecs::Entity entity = world.create();
    world.emplace<Fluid2D>(entity, Fluid2DParams{.radius = kRadius, .max_particles = 1u << 20});
    (void) world.get<Fluid2D>(entity).spawn_box({0.f, 0.f}, {column, column});
    const auto count = static_cast<int>(world.get<Fluid2D>(entity).count());
    for (int s = 0; s < warmup; ++s) {
        run_fluid_2d(world, jobs);
    }

    double sum_us = 0.0;
    double min_us = 1e9;
    double max_us = 0.0;
    for (int s = 0; s < steps; ++s) {
        const auto start = Clock::now();
        run_fluid_2d(world, jobs);
        const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
        sum_us += us;
        min_us = std::min(min_us, us);
        max_us = std::max(max_us, us);
    }

    // Stability: every particle finite, none deeper than half a radius into the tank's walls (inside the cavity, for
    // the compound tank).
    const Fluid2D& fluid = world.get<Fluid2D>(entity);
    bool stable = static_cast<int>(fluid.count()) == count;
    for (const glm::vec2 p : fluid.positions()) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            stable = false;
        } else if (tank == Tank::Sdf) {
            stable = stable && world.get<SdfCollider2D>(terrain).field.sample(p) >= kRadius * 0.5f;
        } else {
            stable = stable && p.x >= kRadius * 0.5f && p.x <= width - kRadius * 0.5f && p.y >= kRadius * 0.5f;
        }
    }

    const double mean_us = steps > 0 ? sum_us / steps : 0.0;
    return BenchResult{
            .name = name,
            .steps = steps,
            .total_entities = 2,
            .dynamic_bodies = count,
            .static_bodies = 1,
            .total_ms = sum_us / 1000.0,
            .mean_us = mean_us,
            .min_us = min_us,
            .max_us = max_us,
            .sleeping_at_end = 0,
            .steps_to_sleep = -1,
            .approx_memory_kb = static_cast<std::size_t>(count) * (5 * sizeof(float)) / 1024,
            .passed_stability = stable,
            .notes = std::to_string(mean_us * 1000.0 / std::max(1, count)) + " ns per particle",
    };
}

}

std::vector<BenchResult> run_fluid_2d_benchmarks(const BenchOptions& options, JobSystem* jobs) {
    const int steps = options.steps_override > 0 ? options.steps_override : 300;
    struct Case {
        const char* name;
        int side;
        int warmup;
        Tank tank;
    };
    const Case cases[] = {
            {"fluid_dam_4k", 64, 0, Tank::Sdf},
            {"fluid_dam_16k", 128, 0, Tank::Sdf},
            {"fluid_rest_4k", 64, 600, Tank::Sdf},
            {"fluid_compound_dam_4k", 64, 0, Tank::Compound},
            {"fluid_compound_rest_4k", 64, 600, Tank::Compound},
    };
    std::vector<BenchResult> results;
    for (const Case& c : cases) {
        if (matches(c.name, options.filter)) {
            results.push_back(bench_dam(c.name, c.side, steps, c.warmup, c.tank, jobs));
        }
    }
    return results;
}

}
