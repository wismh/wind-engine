#include "bench_static_2d.h"

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace engine::bench {
namespace {

using Clock = std::chrono::steady_clock;

inline constexpr int kTerrainColumns = 100;
inline constexpr int kWarmupSteps = 240;
inline constexpr int kEditEvery = 30;
inline constexpr int kDroppers = 64;

// How the terrain cells become entities: one entity per cell, one per cell under 9 chunk parents, or 9 chunk entities
// each with one CompoundCollider2D (LQ-23).
enum class Layout { Separate, Parented, Compound };

struct StaticScene {
    ecs::World world;
    std::vector<ecs::Entity> terrain;  // Separate / Parented: one per cell. Compound: the 9 chunks
    std::vector<ecs::Entity> fallers;
    int dynamic_count{0};
    int static_count{0};
};

struct TickStats {
    double sum_us{0.0};
    double min_us{1e9};
    double max_us{0.0};
    int count{0};

    void add(double us) {
        sum_us += us;
        min_us = std::min(min_us, us);
        max_us = std::max(max_us, us);
        ++count;
    }

    [[nodiscard]] double mean() const {
        return count > 0 ? sum_us / count : 0.0;
    }
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

ecs::Entity add_dynamic_box(ecs::World& world, glm::vec3 position, BodyType type, glm::vec3 velocity) {
    const ecs::Entity entity = world.create();
    world.emplace<Transform>(entity, Transform{.position = position});
    world.emplace<BoxCollider2D>(entity, BoxCollider2D{.size = {1.f, 1.f, 1.f}, .friction = 0.5f});
    world.emplace<RigidBody2D>(entity, RigidBody2D{.velocity = velocity, .type = type});
    return entity;
}

// Marching-squares-like cells: a 100-column grid below y = 0, alternating boxes (every seventh one rotated) and
// convex polygons (quads and triangles). The cell shapes are the same in every layout.
[[nodiscard]] StaticScene build_scene(int static_count, Layout layout) {
    const bool parented = layout == Layout::Parented;
    const bool compound = layout == Layout::Compound;
    StaticScene scene;
    ecs::World& world = scene.world;
    world.ctx<Physics2DConfig>().gravity = glm::vec3{0.f, -20.f, 0.f};
    world.ctx<Time>().fixed_delta_time = kFixed;

    const int rows = (static_count + kTerrainColumns - 1) / kTerrainColumns;
    const float chunk_w = static_cast<float>(kTerrainColumns) / 3.f;
    const float chunk_h = static_cast<float>(rows) / 3.f;
    ecs::Entity chunks[9]{};
    if (parented || compound) {
        for (int i = 0; i < 9; ++i) {
            chunks[i] = world.create();
            world.emplace<Transform>(chunks[i], Transform{.position = {static_cast<float>(i % 3) * chunk_w,
                                                                       -static_cast<float>(i / 3) * chunk_h, 0.f}});
            if (compound) {
                world.emplace<CompoundCollider2D>(chunks[i], CompoundCollider2D{.friction = 0.4f});
                scene.terrain.push_back(chunks[i]);
            }
        }
    }

    for (int i = 0; i < static_count; ++i) {
        const int col = i % kTerrainColumns;
        const int row = i / kTerrainColumns;
        const glm::vec3 position{static_cast<float>(col) + 0.5f, -static_cast<float>(row) - 0.5f, 0.f};
        if (compound) {
            const int chunk_x = std::min(2, static_cast<int>(static_cast<float>(col) / chunk_w));
            const int chunk_y = std::min(2, static_cast<int>(static_cast<float>(row) / chunk_h));
            const ecs::Entity chunk = chunks[chunk_y * 3 + chunk_x];
            const glm::vec3 local3 = position - world.get<Transform>(chunk).position;
            const glm::vec2 local{local3.x, local3.y};
            CompoundShape2D shape;
            if (i % 2 == 0) {
                shape = box_shape_2d(local, {1.f, 1.f}, i % 7 == 0 ? 0.3f : 0.f);
            } else if (i % 3 == 0) {
                shape.vertices[0] = local + glm::vec2{-0.5f, -0.5f};
                shape.vertices[1] = local + glm::vec2{0.5f, -0.5f};
                shape.vertices[2] = local + glm::vec2{-0.5f, 0.5f};
                shape.count = 3;
            } else {
                shape.vertices[0] = local + glm::vec2{-0.5f, -0.5f};
                shape.vertices[1] = local + glm::vec2{0.5f, -0.5f};
                shape.vertices[2] = local + glm::vec2{0.5f, 0.2f};
                shape.vertices[3] = local + glm::vec2{-0.5f, 0.4f};
                shape.count = 4;
            }
            world.get<CompoundCollider2D>(chunk).shapes.push_back(shape);
            continue;
        }
        const ecs::Entity cell = world.create();
        Transform transform{.position = position};
        if (i % 2 == 0 && i % 7 == 0) {
            transform.rotation = rotation_z(0.3f);
        }
        if (parented) {
            const int chunk_x = std::min(2, static_cast<int>(static_cast<float>(col) / chunk_w));
            const int chunk_y = std::min(2, static_cast<int>(static_cast<float>(row) / chunk_h));
            const ecs::Entity chunk = chunks[chunk_y * 3 + chunk_x];
            transform.position -= world.get<Transform>(chunk).position;
            world.emplace<Transform>(cell, transform);
            (void) ecs::set_parent(world, cell, chunk);
        } else {
            world.emplace<Transform>(cell, transform);
        }
        if (i % 2 == 0) {
            world.emplace<BoxCollider2D>(cell, BoxCollider2D{.size = {1.f, 1.f, 1.f}, .friction = 0.4f});
        } else {
            PolygonCollider2D polygon{.friction = 0.4f};
            if (i % 3 == 0) {
                polygon.vertices[0] = {-0.5f, -0.5f};
                polygon.vertices[1] = {0.5f, -0.5f};
                polygon.vertices[2] = {-0.5f, 0.5f};
                polygon.count = 3;
            } else {
                polygon.vertices[0] = {-0.5f, -0.5f};
                polygon.vertices[1] = {0.5f, -0.5f};
                polygon.vertices[2] = {0.5f, 0.2f};
                polygon.vertices[3] = {-0.5f, 0.4f};
                polygon.count = 4;
            }
            world.emplace<PolygonCollider2D>(cell, polygon);
        }
        scene.terrain.push_back(cell);
    }

    // One far platform carrying 4 dynamic boxes that settle and sleep.
    const ecs::Entity platform = world.create();
    world.emplace<Transform>(platform, Transform{.position = {-300.f, 0.f, 0.f}});
    world.emplace<BoxCollider2D>(platform, BoxCollider2D{.size = {20.f, 1.f, 1.f}, .friction = 0.5f});
    for (int i = 0; i < 4; ++i) {
        (void) add_dynamic_box(world, glm::vec3{-306.f + 4.f * static_cast<float>(i), 1.f, 0.f}, BodyType::Dynamic,
                               glm::vec3{0.f});
    }
    // 4 awake dynamic boxes in free fall far from everything, and 2 kinematic movers above the terrain.
    for (int i = 0; i < 4; ++i) {
        scene.fallers.push_back(add_dynamic_box(world, glm::vec3{-600.f - 4.f * static_cast<float>(i), 1000.f, 0.f},
                                                BodyType::Dynamic, glm::vec3{0.f}));
    }
    (void) add_dynamic_box(world, glm::vec3{10.f, 20.f, 0.f}, BodyType::Kinematic, glm::vec3{3.f, 0.f, 0.f});
    (void) add_dynamic_box(world, glm::vec3{60.f, 25.f, 0.f}, BodyType::Kinematic, glm::vec3{-3.f, 0.f, 0.f});

    scene.dynamic_count = 10;
    // Static entities: the cells (or the 9 compound chunks), the chunk parents, and the platform.
    scene.static_count = (compound ? 9 : static_count) + (parented ? 9 : 0) + 1;
    return scene;
}

void step(ecs::World& world, JobSystem* jobs) {
    run_physics_2d(world, jobs);
}

void reset_fallers(StaticScene& scene) {
    for (std::size_t i = 0; i < scene.fallers.size(); ++i) {
        scene.world.get<Transform>(scene.fallers[i]).position =
                glm::vec3{-600.f - 4.f * static_cast<float>(i), 1000.f, 0.f};
        RigidBody2D& body = scene.world.get<RigidBody2D>(scene.fallers[i]);
        body.velocity = glm::vec3{0.f};
        body.sleeping = false;
    }
}

// kDroppers dynamic unit boxes just above the terrain top, one per 1.5 columns: they land, settle and sleep.
void add_droppers(StaticScene& scene) {
    for (int i = 0; i < kDroppers; ++i) {
        const float x = 1.5f * static_cast<float>(i) + 0.75f;
        (void) add_dynamic_box(scene.world, glm::vec3{x, 1.5f + 0.25f * static_cast<float>(i % 3), 0.f},
                               BodyType::Dynamic, glm::vec3{0.f});
    }
    scene.dynamic_count += kDroppers;
}

void warm_up(StaticScene& scene, JobSystem* jobs) {
    for (int i = 0; i < kWarmupSteps; ++i) {
        step(scene.world, jobs);
        scene.world.flush_events();
    }
    reset_fallers(scene);
    // One untimed step so the first measured tick does not pay for the reset.
    step(scene.world, jobs);
    scene.world.flush_events();
}

[[nodiscard]] int count_sleeping(ecs::World& world) {
    int sleeping = 0;
    world.view<RigidBody2D>().each([&](const RigidBody2D& body) {
        if (body.type == BodyType::Dynamic && body.sleeping) {
            ++sleeping;
        }
    });
    return sleeping;
}

[[nodiscard]] BenchResult make_result(std::string name, const StaticScene& scene, const TickStats& stats, int sleeping,
                                      std::string notes, int expected_sleeping = 4) {
    return BenchResult{
            .name = std::move(name),
            .steps = stats.count,
            .total_entities = scene.dynamic_count + scene.static_count,
            .dynamic_bodies = scene.dynamic_count,
            .static_bodies = scene.static_count,
            .total_ms = stats.sum_us / 1000.0,
            .mean_us = stats.mean(),
            .min_us = stats.min_us,
            .max_us = stats.max_us,
            .sleeping_at_end = sleeping,
            .steps_to_sleep = -1,
            .approx_memory_kb = 0,
            .passed_stability = sleeping >= expected_sleeping,
            .notes = std::move(notes),
    };
}

[[nodiscard]] BenchResult bench_static_far(std::string name, int static_count, Layout layout, int steps,
                                           JobSystem* jobs) {
    StaticScene scene = build_scene(static_count, layout);
    warm_up(scene, jobs);
    TickStats stats;
    for (int i = 0; i < steps; ++i) {
        const auto start = Clock::now();
        step(scene.world, jobs);
        stats.add(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
        scene.world.flush_events();
    }
    return make_result(std::move(name), scene, stats, count_sleeping(scene.world),
                       "run_physics_2d only; statics untouched");
}

// After an in-place static edit. Before LQ-22 the engine noticed it on its own; now the caller signals it.
void signal_static_edit(ecs::World& world) {
    mark_static_colliders_dirty_2d(world);
}

// After warm-up, kDroppers dynamic boxes are dropped onto the terrain; every tick from the drop on is timed, so the
// row covers the landing, the resting contacts and the fall asleep.
[[nodiscard]] BenchResult bench_static_contact(std::string name, Layout layout, int steps, JobSystem* jobs) {
    StaticScene scene = build_scene(2000, layout);
    warm_up(scene, jobs);
    add_droppers(scene);
    // The collider adds re-read the static cache on this tick; it is not timed.
    step(scene.world, jobs);
    scene.world.flush_events();
    TickStats stats;
    for (int i = 0; i < steps; ++i) {
        const auto start = Clock::now();
        step(scene.world, jobs);
        stats.add(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
        scene.world.flush_events();
    }
    return make_result(std::move(name), scene, stats, count_sleeping(scene.world),
                       "run_physics_2d only; 64 boxes land on the terrain", 4 + kDroppers);
}

// Every kEditEvery ticks: either move one terrain cell in place (`spawn` false) or spawn a dynamic box with a
// collider far away (`spawn` true). The edit ticks and the other ticks are reported as separate rows. In the compound
// layout the edit moves one shape of a chunk in place.
void bench_static_churn(std::vector<BenchResult>& out, std::string name, bool spawn, Layout layout, int steps,
                        JobSystem* jobs) {
    StaticScene scene = build_scene(2000, layout);
    warm_up(scene, jobs);
    TickStats edit_ticks;
    TickStats other_ticks;
    std::size_t next_cell = 0;
    for (int i = 0; i < steps; ++i) {
        const bool edit = (i % kEditEvery) == 0;
        if (edit) {
            if (spawn) {
                (void) add_dynamic_box(scene.world, glm::vec3{-900.f - static_cast<float>(i), 500.f, 0.f},
                                       BodyType::Dynamic, glm::vec3{0.f});
            } else {
                if (layout == Layout::Compound) {
                    CompoundCollider2D& chunk =
                            scene.world.get<CompoundCollider2D>(scene.terrain[next_cell % scene.terrain.size()]);
                    CompoundShape2D& shape = chunk.shapes[(next_cell / scene.terrain.size()) % chunk.shapes.size()];
                    for (int v = 0; v < shape.count; ++v) {
                        shape.vertices[v].x += 0.001f;
                    }
                    next_cell = (next_cell + 37) % 2000;
                } else {
                    const ecs::Entity cell = scene.terrain[next_cell];
                    next_cell = (next_cell + 37) % scene.terrain.size();
                    scene.world.get<Transform>(cell).position.x += 0.001f;
                }
                signal_static_edit(scene.world);
            }
        }
        const auto start = Clock::now();
        step(scene.world, jobs);
        const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
        (edit ? edit_ticks : other_ticks).add(us);
        scene.world.flush_events();
    }
    const int sleeping = count_sleeping(scene.world);
    out.push_back(make_result(name + " edit ticks", scene, edit_ticks, sleeping, "ticks right after the change"));
    out.push_back(make_result(name + " other ticks", scene, other_ticks, sleeping, "steady state between changes"));
}

}

std::vector<BenchResult> run_static_2d_benchmarks(const BenchOptions& options, JobSystem* jobs) {
    const int steps = options.steps_override > 0 ? options.steps_override : 600;
    std::vector<BenchResult> results;
    struct FarCase {
        const char* name;
        int count;
        Layout layout;
    };
    const FarCase far_cases[] = {
            {"static_far_2k", 2000, Layout::Separate},
            {"static_far_4k", 4000, Layout::Separate},
            {"static_far_8k", 8000, Layout::Separate},
            {"static_far_2k_parented", 2000, Layout::Parented},
            {"static_far_2k_compound", 2000, Layout::Compound},
            {"static_far_8k_compound", 8000, Layout::Compound},
    };
    for (const FarCase& c : far_cases) {
        if (matches(c.name, options.filter)) {
            results.push_back(bench_static_far(c.name, c.count, c.layout, steps, jobs));
        }
    }
    if (matches("static_edit_2k", options.filter)) {
        bench_static_churn(results, "static_edit_2k", false, Layout::Separate, steps, jobs);
    }
    if (matches("static_edit_2k_compound", options.filter)) {
        bench_static_churn(results, "static_edit_2k_compound", false, Layout::Compound, steps, jobs);
    }
    if (matches("static_spawn_2k", options.filter)) {
        bench_static_churn(results, "static_spawn_2k", true, Layout::Separate, steps, jobs);
    }
    if (matches("static_spawn_2k_compound", options.filter)) {
        bench_static_churn(results, "static_spawn_2k_compound", true, Layout::Compound, steps, jobs);
    }
    struct ContactCase {
        const char* name;
        Layout layout;
    };
    const ContactCase contact_cases[] = {
            {"static_contact_2k", Layout::Separate},
            {"static_contact_2k_compound", Layout::Compound},
    };
    for (const ContactCase& c : contact_cases) {
        if (matches(c.name, options.filter)) {
            results.push_back(bench_static_contact(c.name, c.layout, steps, jobs));
        }
    }
    return results;
}

}
