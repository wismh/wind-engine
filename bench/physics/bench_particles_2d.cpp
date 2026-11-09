#include "bench_particles_2d.h"

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/particles.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::bench {
namespace {

using Clock = std::chrono::steady_clock;

inline constexpr int kTerrainColumns = 100;
inline constexpr int kTerrainRows = 20;
inline constexpr float kFrameDt = 1.f / 60.f;

// What the particles collide with.
enum class Boundary {
    // collision_enabled off: the integrate cost alone.
    None,
    // One BoxCollider2D floor under the whole block (top at y = 0).
    BoxFloor,
    // The static-terrain bench's field (100 x 20, 0.25-unit cells, a 1-unit band, a crater every 10 columns).
    SdfTerrain,
    // The same field, with the particles 200 units to the side: only the bounds reject runs.
    SdfFar,
    // A solid world (SdfOutside::Solid) with one round cavity of radius 8 the particles start in: a pool where every
    // particle ends up resting on the surface (particles do not collide with each other).
    SdfPool,
};

[[nodiscard]] bool matches(std::string_view name, std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    const auto it = std::search(name.begin(), name.end(), filter.begin(), filter.end(),
                                [&](char a, char b) { return lower(a) == lower(b); });
    return it != name.end();
}

[[nodiscard]] SdfField2D terrain_field() {
    SdfField2D field{SdfField2DDesc{.cell_size = 0.25f, .band = 1.f}};
    const float depth = static_cast<float>(kTerrainRows);
    const glm::vec2 block[4] = {{0.f, -depth}, {static_cast<float>(kTerrainColumns), -depth},
                                {static_cast<float>(kTerrainColumns), 0.f}, {0.f, 0.f}};
    (void) field.fill_polygon(block);
    for (int x = 5; x < kTerrainColumns; x += 10) {
        (void) field.carve_circle({static_cast<float>(x), 0.f}, 1.5f);
    }
    return field;
}

[[nodiscard]] SdfField2D pool_field() {
    SdfField2D field{SdfField2DDesc{.cell_size = 0.25f, .band = 1.f, .outside = SdfOutside::Solid}};
    (void) field.carve_circle({50.f, 4.f}, 8.f);
    return field;
}

// `count` particles on a square grid: over the terrain (x in [10, 90], y in [1, 9]), or inside the pool's cavity.
void place_particles(render::ParticleEmitter& emitter, int count, Boundary boundary) {
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
    const bool pool = boundary == Boundary::SdfPool;
    const glm::vec2 lo = pool ? glm::vec2{45.f, 0.f} : glm::vec2{10.f, 1.f};
    const glm::vec2 size = pool ? glm::vec2{10.f, 10.f} : glm::vec2{80.f, 8.f};
    const float shift = boundary == Boundary::SdfFar ? 200.f : 0.f;
    emitter.particles.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float u = static_cast<float>(i % side) / static_cast<float>(side);
        const float v = static_cast<float>(i / side) / static_cast<float>(side);
        render::Particle p;
        p.position = {lo.x + size.x * u + shift, lo.y + size.y * v, 0.f};
        const float a = static_cast<float>(i) * 2.399963f;
        p.velocity = {2.f * std::cos(a), 2.f * std::sin(a), 0.f};
        p.lifetime = 1.0e6f;
        emitter.particles.push_back(p);
    }
}

[[nodiscard]] BenchResult bench_particles(
        const std::string& name, int count, Boundary boundary, int steps, JobSystem* jobs) {
    ecs::World world;
    world.ctx<Time>().delta_time = kFrameDt;
    int static_count = 0;
    if (boundary == Boundary::BoxFloor) {
        const ecs::Entity floor = world.create();
        world.emplace<Transform>(floor, Transform{.position = {50.f, -10.f, 0.f}});
        world.emplace<BoxCollider2D>(floor, BoxCollider2D{.size = {100.f, 20.f, 1.f}});
        static_count = 1;
    } else if (boundary == Boundary::SdfTerrain || boundary == Boundary::SdfFar || boundary == Boundary::SdfPool) {
        const ecs::Entity terrain = world.create();
        world.emplace<Transform>(terrain, Transform{});
        world.emplace<SdfCollider2D>(
                terrain,
                SdfCollider2D{.field = boundary == Boundary::SdfPool ? pool_field() : terrain_field()});
        static_count = 1;
    }

    const ecs::Entity entity = world.create();
    world.emplace<Transform>(entity, Transform{});
    render::ParticleEmitter emitter;
    emitter.emission_rate = 0.f;
    emitter.playing = false;
    emitter.max_particles = static_cast<std::size_t>(count);
    emitter.gravity = {0.f, -10.f, 0.f};
    emitter.collision_enabled = boundary != Boundary::None;
    emitter.collision_radius = 0.05f;
    emitter.bounce = 0.2f;
    emitter.friction = 0.1f;
    place_particles(emitter, count, boundary);
    world.emplace<render::ParticleEmitter>(entity, std::move(emitter));

    double sum_us = 0.0;
    double min_us = 1e9;
    double max_us = 0.0;
    for (int s = 0; s < steps; ++s) {
        const auto start = Clock::now();
        run_particles(world, jobs);
        const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
        sum_us += us;
        min_us = std::min(min_us, us);
        max_us = std::max(max_us, us);
    }

    // Stability: with collision on, no particle ends inside the solid.
    bool stable = true;
    const render::ParticleEmitter& done = world.get<render::ParticleEmitter>(entity);
    if (boundary == Boundary::SdfTerrain || boundary == Boundary::SdfPool) {
        world.view<SdfCollider2D>().each([&](ecs::Entity, const SdfCollider2D& sdf) {
            for (const render::Particle& p : done.particles) {
                if (sdf.field.sample({p.position.x, p.position.y}) < 0.f) {
                    stable = false;
                }
            }
        });
    }
    if (static_cast<int>(done.active_count()) != count) {
        stable = false;
    }

    const double mean_us = steps > 0 ? sum_us / steps : 0.0;
    return BenchResult{
            .name = name,
            .steps = steps,
            .total_entities = 1 + static_count,
            .dynamic_bodies = count,
            .static_bodies = static_count,
            .total_ms = sum_us / 1000.0,
            .mean_us = mean_us,
            .min_us = min_us,
            .max_us = max_us,
            .sleeping_at_end = 0,
            .steps_to_sleep = -1,
            .approx_memory_kb = static_cast<std::size_t>(count) * sizeof(render::Particle) / 1024,
            .passed_stability = stable,
            .notes = std::to_string(mean_us * 1000.0 / std::max(1, count)) + " ns per particle",
    };
}

}

std::vector<BenchResult> run_particle_2d_benchmarks(const BenchOptions& options, JobSystem* jobs) {
    const int steps = options.steps_override > 0 ? options.steps_override : 600;
    struct Case {
        const char* name;
        int count;
        Boundary boundary;
    };
    const Case cases[] = {
            {"particles_free_4k", 4096, Boundary::None},
            {"particles_box_floor_4k", 4096, Boundary::BoxFloor},
            {"particles_sdf_far_4k", 4096, Boundary::SdfFar},
            {"particles_sdf_terrain_4k", 4096, Boundary::SdfTerrain},
            {"particles_sdf_terrain_16k", 16384, Boundary::SdfTerrain},
            {"particles_sdf_pool_4k", 4096, Boundary::SdfPool},
    };
    std::vector<BenchResult> results;
    for (const Case& c : cases) {
        if (matches(c.name, options.filter)) {
            results.push_back(bench_particles(c.name, c.count, c.boundary, steps, jobs));
        }
    }
    return results;
}

}
