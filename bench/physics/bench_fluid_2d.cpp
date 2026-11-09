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

// A `side` x `side` column of particles at rest spacing against the tank's left wall, released at once.
[[nodiscard]] BenchResult bench_dam(const std::string& name, int side, int steps, JobSystem* jobs) {
    const float column = static_cast<float>(side) * 2.f * kRadius;
    const float width = column * 2.5f;
    ecs::World world;
    world.ctx<Time>().fixed_delta_time = kFixed;
    world.ctx<Physics2DConfig>().gravity = {0.f, -10.f, 0.f};
    const ecs::Entity terrain = world.create();
    world.emplace<Transform>(terrain, Transform{});
    world.emplace<SdfCollider2D>(terrain, SdfCollider2D{.field = tank_field(width, column * 1.5f)});
    const ecs::Entity entity = world.create();
    world.emplace<Fluid2D>(entity, Fluid2DParams{.radius = kRadius, .max_particles = 1u << 20});
    (void) world.get<Fluid2D>(entity).spawn_box({0.f, 0.f}, {column, column});
    const auto count = static_cast<int>(world.get<Fluid2D>(entity).count());

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

    // Stability: every particle finite, none deeper than half a radius into the tank's walls.
    const Fluid2D& fluid = world.get<Fluid2D>(entity);
    const SdfField2D& field = world.get<SdfCollider2D>(terrain).field;
    bool stable = static_cast<int>(fluid.count()) == count;
    for (const glm::vec2 p : fluid.positions()) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || field.sample(p) < kRadius * 0.5f) {
            stable = false;
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
    };
    const Case cases[] = {
            {"fluid_dam_4k", 64},
            {"fluid_dam_16k", 128},
    };
    std::vector<BenchResult> results;
    for (const Case& c : cases) {
        if (matches(c.name, options.filter)) {
            results.push_back(bench_dam(c.name, c.side, steps, jobs));
        }
    }
    return results;
}

}
