#include <gtest/gtest.h>

#include <engine/core/det_math.h>
#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>

#include "ecs/fluid_2d_internal.h"
#include "ecs/physics_2d_internal.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <tuple>
#include <vector>

// LQ-27: Fluid2D, position based fluids with SDF and collider boundaries.

namespace {

constexpr float kRadius = 0.05f;
constexpr float kCell = 0.1f;
constexpr float kBand = 0.4f;

void step(engine::ecs::World& world, int steps = 1) {
    world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    for (int i = 0; i < steps; ++i) {
        world.run(engine::ecs::Schedule::Fixed);
    }
}

engine::ecs::World fluid_world(engine::JobSystem* jobs = nullptr, glm::vec3 gravity = {0.f, -10.f, 0.f}) {
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.jobs = jobs});
    engine::install_fluid_2d(world, jobs);
    world.ctx<engine::Physics2DConfig>().gravity = gravity;
    return world;
}

// An open-top tank: solid walls 1 unit thick around the cavity [0, width] x [0, height + 2].
engine::SdfField2D tank_field(float width = 4.f, float height = 3.f) {
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    const std::array<glm::vec2, 4> outer{glm::vec2{-1.f, -1.f}, glm::vec2{width + 1.f, -1.f},
                                         glm::vec2{width + 1.f, height}, glm::vec2{-1.f, height}};
    const std::array<glm::vec2, 4> inner{glm::vec2{0.f, 0.f}, glm::vec2{width, 0.f}, glm::vec2{width, height + 2.f},
                                         glm::vec2{0.f, height + 2.f}};
    (void)field.fill_polygon(outer);
    (void)field.carve_polygon(inner);
    return field;
}

// Solid below y = 0 over x in [-3, 7].
engine::SdfField2D floor_field() {
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    const std::array<glm::vec2, 4> ground{glm::vec2{-3.f, -1.f}, glm::vec2{7.f, -1.f}, glm::vec2{7.f, 0.f},
                                          glm::vec2{-3.f, 0.f}};
    (void)field.fill_polygon(ground);
    return field;
}

engine::ecs::Entity add_terrain(engine::ecs::World& world, engine::SdfField2D field, uint32_t layer = 1,
                                uint32_t mask = 0xFFFFFFFFu) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::SdfCollider2D>(
            entity, engine::SdfCollider2D{.field = std::move(field), .layer = layer, .mask = mask});
    return entity;
}

engine::ecs::Entity add_fluid(engine::ecs::World& world,
                              engine::Fluid2DParams params = engine::Fluid2DParams{.radius = kRadius}) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Fluid2D>(entity, params);
    return entity;
}

// A floating particle at (1, 0.5) and a kinematic paddle at x = 0.5 that run_physics_2d moves to x = 1 on the first
// tick. A fluid step after physics sees the paddle on the particle and pushes it; one before physics does not.
engine::ecs::Entity order_probe(engine::ecs::World& world) {
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, 0.f, 0.f};
    const engine::ecs::Entity paddle = world.create();
    world.emplace<engine::Transform>(paddle, engine::Transform{.position = {0.5f, 0.5f, 0.f}});
    world.emplace<engine::BoxCollider2D>(paddle, engine::BoxCollider2D{.size = {0.2f, 0.8f, 1.f}});
    world.emplace<engine::RigidBody2D>(
            paddle, engine::RigidBody2D{.velocity = {0.5f / engine::kFixed, 0.f, 0.f},
                                        .type = engine::BodyType::Kinematic});
    const engine::ecs::Entity entity = add_fluid(world);
    (void)world.get<engine::Fluid2D>(entity).spawn({1.f, 0.5f});
    return entity;
}

float kinetic_energy(const engine::Fluid2D& fluid) {
    float sum = 0.f;
    for (const glm::vec2 v : fluid.velocities()) {
        sum += 0.5f * glm::dot(v, v);
    }
    return sum;
}

float min_distance(const engine::Fluid2D& fluid, const engine::SdfField2D& field) {
    float lowest = 1.0e9f;
    for (const glm::vec2 p : fluid.positions()) {
        lowest = std::min(lowest, field.sample(p));
    }
    return lowest;
}

bool all_finite(const engine::Fluid2D& fluid) {
    for (const glm::vec2 p : fluid.positions()) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            return false;
        }
    }
    return true;
}

}

TEST(Fluid2D, SingleParticleFallsUnderGravity) {
    engine::ecs::World world = fluid_world();
    const engine::ecs::Entity entity = add_fluid(world);
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({1.f, 2.f}, {0.5f, 0.f}));

    step(world, 60);

    // Symplectic Euler over 120 substeps of 1/120 s: y = 2 - g dt^2 n (n + 1) / 2.
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    ASSERT_EQ(fluid.count(), 1u);
    const float dt = engine::kFixed / 2.f;
    EXPECT_NEAR(fluid.positions()[0].y, 2.f - 10.f * dt * dt * 120.f * 121.f / 2.f, 1.0e-3f);
    EXPECT_NEAR(fluid.positions()[0].x, 1.5f, 1.0e-3f);
    EXPECT_NEAR(fluid.velocities()[0].y, -10.f, 1.0e-3f);
    EXPECT_NEAR(fluid.velocities()[0].x, 0.5f, 1.0e-3f);
}

TEST(Fluid2D, GravityScaleZeroFloats) {
    engine::ecs::World world = fluid_world();
    const engine::ecs::Entity entity =
            add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .gravity_scale = 0.f});
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({1.f, 2.f}));
    step(world, 30);
    EXPECT_EQ(world.get<engine::Fluid2D>(entity).positions()[0], glm::vec2(1.f, 2.f));
}

TEST(Fluid2D, NeighborGridMatchesBruteForce) {
    // A clumped and a scattered set, across negative coordinates and hash collisions.
    std::vector<glm::vec2> positions;
    std::uint32_t state = 12345u;
    auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
    };
    for (int i = 0; i < 600; ++i) {
        positions.push_back({next() * 3.f - 1.5f, next() * 3.f - 1.5f});
    }
    for (int i = 0; i < 200; ++i) {
        positions.push_back({next() * 400.f - 200.f, next() * 400.f - 200.f});
    }
    const float h = 0.3f;
    engine::JobSystem jobs(3);
    const engine::FluidNeighbors2D serial = engine::fluid_neighbors_2d(positions, h, nullptr);
    const engine::FluidNeighbors2D parallel = engine::fluid_neighbors_2d(positions, h, &jobs);
    EXPECT_EQ(serial.count, parallel.count);
    EXPECT_EQ(serial.list, parallel.list);

    for (std::size_t i = 0; i < positions.size(); ++i) {
        std::vector<std::uint32_t> expected;
        for (std::size_t j = 0; j < positions.size(); ++j) {
            const glm::vec2 d = positions[i] - positions[j];
            if (j != i && glm::dot(d, d) < h * h) {
                expected.push_back(static_cast<std::uint32_t>(j));
            }
        }
        ASSERT_LE(expected.size(), engine::kFluidMaxNeighbors2D);
        const auto begin = serial.list.begin() + static_cast<std::ptrdiff_t>(i * engine::kFluidMaxNeighbors2D);
        std::vector<std::uint32_t> found(begin, begin + serial.count[i]);
        std::sort(found.begin(), found.end());
        EXPECT_EQ(found, expected) << "particle " << i;
    }
}

TEST(Fluid2D, DamBreakSettlesInSdfTank) {
    engine::ecs::World world = fluid_world();
    const engine::ecs::Entity terrain = add_terrain(world, tank_field());
    const engine::ecs::Entity entity = add_fluid(world);
    const std::size_t spawned = world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {1.2f, 2.4f});
    ASSERT_EQ(spawned, 12u * 24u);

    float peak = 0.f;
    for (int s = 0; s < 420; ++s) {
        step(world);
        peak = std::max(peak, kinetic_energy(world.get<engine::Fluid2D>(entity)));
    }
    // The tail: settled over the last second.
    float tail_max_density = 0.f;
    for (int s = 0; s < 60; ++s) {
        step(world);
        for (const float d : world.get<engine::Fluid2D>(entity).densities()) {
            tail_max_density = std::max(tail_max_density, d);
        }
    }

    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    const engine::SdfField2D& field = world.get<engine::SdfCollider2D>(terrain).field;
    EXPECT_EQ(fluid.count(), spawned);
    EXPECT_TRUE(all_finite(fluid));
    EXPECT_GE(min_distance(fluid, field), kRadius * 0.5f);
    EXPECT_GT(peak, 0.f);
    EXPECT_LT(kinetic_energy(fluid), peak * 0.02f);
    // The column spread over the tank floor.
    float max_x = 0.f;
    float max_y = 0.f;
    for (const glm::vec2 p : fluid.positions()) {
        max_x = std::max(max_x, p.x);
        max_y = std::max(max_y, p.y);
    }
    EXPECT_GT(max_x, 3.f);
    EXPECT_LT(max_y, 1.6f);
    // Compression stays bounded (the solver only pushes when density exceeds rest).
    EXPECT_LT(tail_max_density, 1.2f);
}

TEST(Fluid2D, BitIdenticalAcrossWorkerCounts) {
    auto simulate = [](engine::JobSystem* jobs) {
        engine::ecs::World world = fluid_world(jobs);
        (void)add_terrain(world, tank_field());
        const engine::ecs::Entity entity = add_fluid(world);
        // Over kGrain particles several times, so the ranges split across workers.
        (void)world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {2.f, 2.f}, {0.5f, 0.f});
        step(world, 45);
        const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
        std::vector<std::uint32_t> bits;
        for (std::size_t i = 0; i < fluid.count(); ++i) {
            bits.push_back(std::bit_cast<std::uint32_t>(fluid.positions()[i].x));
            bits.push_back(std::bit_cast<std::uint32_t>(fluid.positions()[i].y));
            bits.push_back(std::bit_cast<std::uint32_t>(fluid.velocities()[i].x));
            bits.push_back(std::bit_cast<std::uint32_t>(fluid.velocities()[i].y));
        }
        return bits;
    };
    engine::JobSystem jobs(3);
    const std::vector<std::uint32_t> serial = simulate(nullptr);
    const std::vector<std::uint32_t> parallel = simulate(&jobs);
    ASSERT_EQ(serial.size(), 400u * 4u);
    EXPECT_EQ(serial, parallel);
}

TEST(Fluid2D, KinematicBoxSweepsThroughFluid) {
    engine::ecs::World world = fluid_world();
    (void)add_terrain(world, floor_field());
    const engine::ecs::Entity entity = add_fluid(world);
    (void)world.get<engine::Fluid2D>(entity).spawn_box({1.f, 0.f}, {3.f, 0.6f});
    step(world, 60);

    const engine::ecs::Entity paddle = world.create();
    world.emplace<engine::Transform>(paddle, engine::Transform{.position = {0.f, 0.5f, 0.f}});
    // Spans y in [0.1, 0.9]: one particle diameter above the floor.
    world.emplace<engine::BoxCollider2D>(paddle, engine::BoxCollider2D{.size = {0.2f, 0.8f, 1.f}});
    world.emplace<engine::RigidBody2D>(
            paddle, engine::RigidBody2D{.velocity = {2.f, 0.f, 0.f}, .type = engine::BodyType::Kinematic});

    float mean_before = 0.f;
    for (const glm::vec2 p : world.get<engine::Fluid2D>(entity).positions()) {
        mean_before += p.x;
    }
    mean_before /= static_cast<float>(world.get<engine::Fluid2D>(entity).count());

    for (int s = 0; s < 60; ++s) {
        step(world);
        const float px = world.get<engine::Transform>(paddle).position.x;
        for (const glm::vec2 p : world.get<engine::Fluid2D>(entity).positions()) {
            // No particle center inside the paddle (shrunk by half a radius), none in the floor.
            ASSERT_GE(p.y, kRadius * 0.5f);
            const bool inside = std::abs(p.x - px) < 0.1f - kRadius * 0.5f && p.y > 0.1f + kRadius * 0.5f &&
                                p.y < 0.9f - kRadius * 0.5f;
            ASSERT_FALSE(inside) << "step " << s << " particle at " << p.x << ", " << p.y << " paddle " << px;
        }
    }
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    float mean_after = 0.f;
    for (const glm::vec2 p : fluid.positions()) {
        mean_after += p.x;
    }
    mean_after /= static_cast<float>(fluid.count());
    // The paddle ends at x = 2: the fluid was pushed ahead of it.
    EXPECT_GT(mean_after, mean_before + 0.3f);
    EXPECT_TRUE(all_finite(fluid));
}

TEST(Fluid2D, WithoutDensityDynamicBodyPushesFluidOneWay) {
    auto simulate = [](bool with_fluid, bool check) {
        engine::ecs::World world = fluid_world();
        (void)add_terrain(world, floor_field());
        const engine::ecs::Entity ball = world.create();
        world.emplace<engine::Transform>(ball, engine::Transform{.position = {2.f, 1.5f, 0.f}});
        world.emplace<engine::CircleCollider2D>(ball, engine::CircleCollider2D{.radius = 0.25f});
        world.emplace<engine::RigidBody2D>(ball, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
        // density 0: the fluid has no mass, so it pushes nothing back.
        const engine::ecs::Entity entity = add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .density = 0.f});
        if (with_fluid) {
            (void)world.get<engine::Fluid2D>(entity).spawn_box({1.f, 0.f}, {3.f, 0.5f});
        }
        step(world, 90);
        if (check) {
            // The ball sits on the floor inside the pool, and no particle overlaps it.
            const glm::vec2 center{world.get<engine::Transform>(ball).position};
            for (const glm::vec2 p : world.get<engine::Fluid2D>(entity).positions()) {
                EXPECT_GE(glm::length(p - center), 0.25f + kRadius * 0.5f);
            }
        }
        return world.get<engine::Transform>(ball).position;
    };
    const glm::vec3 dry = simulate(false, false);
    const glm::vec3 wet = simulate(true, true);
    // The fluid never pushes the body: the same trajectory, bit for bit.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(dry.x), std::bit_cast<std::uint32_t>(wet.x));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(dry.y), std::bit_cast<std::uint32_t>(wet.y));
    EXPECT_LT(wet.y, 0.3f);
}

TEST(Fluid2D, StaticBoxAndCircleBoundIt) {
    engine::ecs::World world = fluid_world();
    const engine::ecs::Entity floor = world.create();
    world.emplace<engine::Transform>(floor, engine::Transform{.position = {2.f, -0.5f, 0.f}});
    world.emplace<engine::BoxCollider2D>(floor, engine::BoxCollider2D{.size = {8.f, 1.f, 1.f}});
    const engine::ecs::Entity rock = world.create();
    world.emplace<engine::Transform>(rock, engine::Transform{.position = {2.f, 0.f, 0.f}});
    world.emplace<engine::CircleCollider2D>(rock, engine::CircleCollider2D{.radius = 0.4f});
    const engine::ecs::Entity entity = add_fluid(world);
    (void)world.get<engine::Fluid2D>(entity).spawn_box({1.5f, 1.f}, {2.5f, 1.5f});

    step(world, 180);

    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    for (const glm::vec2 p : fluid.positions()) {
        EXPECT_GE(p.y, kRadius * 0.5f);
        EXPECT_GE(glm::length(p - glm::vec2{2.f, 0.f}), 0.4f + kRadius * 0.5f);
    }
}

TEST(Fluid2D, LayerAndMaskFilterBoundaries) {
    auto lowest_after_fall = [](uint32_t terrain_layer, uint32_t terrain_mask, uint32_t fluid_mask) {
        engine::ecs::World world = fluid_world();
        (void)add_terrain(world, floor_field(), terrain_layer, terrain_mask);
        const engine::ecs::Entity entity =
                add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .layer = 1u << 3, .mask = fluid_mask});
        (void)world.get<engine::Fluid2D>(entity).spawn_box({1.f, 0.5f}, {1.5f, 1.f});
        step(world, 60);
        float lowest = 1.0e9f;
        for (const glm::vec2 p : world.get<engine::Fluid2D>(entity).positions()) {
            lowest = std::min(lowest, p.y);
        }
        return lowest;
    };
    EXPECT_GT(lowest_after_fall(1u << 1, 0xFFFFFFFFu, 0xFFFFFFFFu), 0.f);
    // The fluid's mask leaves out the terrain's layer.
    EXPECT_LT(lowest_after_fall(1u << 1, 0xFFFFFFFFu, ~(1u << 1)), -1.f);
    // The terrain's mask leaves out the fluid's layer.
    EXPECT_LT(lowest_after_fall(1u << 1, ~(1u << 3), 0xFFFFFFFFu), -1.f);
}

TEST(Fluid2D, SdfUnderDynamicBodyIsSkipped) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity terrain = add_terrain(world, floor_field());
    world.emplace<engine::RigidBody2D>(terrain, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    const engine::ecs::Entity entity =
            add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .gravity_scale = 0.f});
    // Inside the floor: a bounded fluid would be pushed up.
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({1.f, -0.02f}));
    step(world, 5);
    EXPECT_FLOAT_EQ(world.get<engine::Fluid2D>(entity).positions()[0].y, -0.02f);
}

TEST(Fluid2D, SpawnClearAndCap) {
    engine::Fluid2D fluid{engine::Fluid2DParams{.radius = 0.1f, .max_particles = 10}};
    EXPECT_EQ(fluid.spawn_box({0.f, 0.f}, {10.f, 10.f}), 10u);
    EXPECT_EQ(fluid.count(), 10u);
    EXPECT_EQ(fluid.positions()[0], glm::vec2(0.1f, 0.1f));
    EXPECT_FALSE(fluid.spawn({0.f, 0.f}));
    fluid.clear();
    EXPECT_EQ(fluid.count(), 0u);
    EXPECT_TRUE(fluid.positions().empty());
    EXPECT_TRUE(fluid.velocities().empty());
    EXPECT_TRUE(fluid.densities().empty());
    EXPECT_FALSE(fluid.spawn({std::nanf(""), 0.f}));
    EXPECT_FALSE(fluid.spawn({0.f, 0.f}, {INFINITY, 0.f}));
    EXPECT_TRUE(fluid.spawn({1.f, 2.f}, {3.f, 4.f}));
    EXPECT_EQ(fluid.velocities()[0], glm::vec2(3.f, 4.f));
    EXPECT_EQ(fluid.densities().size(), 1u);
    // A 0.4 x 0.2 box at spacing 0.2 holds 2 x 1.
    fluid.clear();
    EXPECT_EQ(fluid.spawn_box({0.f, 0.f}, {0.4f, 0.2f}), 2u);
}

TEST(Fluid2D, SerializerRoundTripsParametersOnly) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    engine::Fluid2D fluid{engine::Fluid2DParams{.radius = 0.08f,
                           .smoothing_length = 0.5f,
                           .rest_density = 7.5f,
                           .viscosity = 0.2f,
                           .relaxation = 0.25f,
                           .tensile_strength = 0.05f,
                           .iterations = 6,
                           .substeps = 3,
                           .max_particles = 4096,
                           .gravity_scale = 0.5f,
                           .friction = 0.3f,
                           .restitution = 0.4f,
                           .density = 0.75f,
                           .cohesion = 6.f,
                           .vorticity = 0.04f,
                           .layer = 1u << 4,
                           .mask = 0x0Fu}};
    (void)fluid.spawn({1.f, 1.f});
    world.emplace<engine::Fluid2D>(entity, fluid);

    const std::string saved = engine::ecs::save_world_to_toml(world);
    EXPECT_NE(saved.find("fluid_2d"), std::string::npos);
    engine::ecs::World loaded_world;
    auto loaded = engine::ecs::load_world_from_toml(loaded_world, saved);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    std::vector<const engine::Fluid2D*> found;
    loaded_world.view<engine::Fluid2D>().each([&](const engine::Fluid2D& fluid) { found.push_back(&fluid); });
    ASSERT_EQ(found.size(), 1u);
    const engine::Fluid2D& c = *found[0];
    EXPECT_FLOAT_EQ(c.params.radius, 0.08f);
    EXPECT_FLOAT_EQ(c.params.smoothing_length, 0.5f);
    EXPECT_FLOAT_EQ(c.params.rest_density, 7.5f);
    EXPECT_FLOAT_EQ(c.params.viscosity, 0.2f);
    EXPECT_FLOAT_EQ(c.params.relaxation, 0.25f);
    EXPECT_FLOAT_EQ(c.params.tensile_strength, 0.05f);
    EXPECT_EQ(c.params.iterations, 6);
    EXPECT_EQ(c.params.substeps, 3);
    EXPECT_EQ(c.params.max_particles, 4096u);
    EXPECT_FLOAT_EQ(c.params.gravity_scale, 0.5f);
    EXPECT_FLOAT_EQ(c.params.friction, 0.3f);
    EXPECT_FLOAT_EQ(c.params.restitution, 0.4f);
    EXPECT_FLOAT_EQ(c.params.density, 0.75f);
    EXPECT_FLOAT_EQ(c.params.cohesion, 6.f);
    EXPECT_FLOAT_EQ(c.params.vorticity, 0.04f);
    EXPECT_EQ(c.params.layer, 1u << 4);
    EXPECT_EQ(c.params.mask, 0x0Fu);
    // Particles are transient.
    EXPECT_EQ(c.count(), 0u);
}

TEST(Fluid2D, NotInstalledDoesNotStep) {
    engine::ecs::World world;
    engine::register_engine_systems(world);
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -10.f, 0.f};
    const engine::ecs::Entity entity = add_fluid(world);
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({1.f, 2.f}, {0.5f, 0.f}));
    step(world, 10);
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    EXPECT_EQ(fluid.positions()[0], glm::vec2(1.f, 2.f));
    EXPECT_EQ(fluid.velocities()[0], glm::vec2(0.5f, 0.f));
}

TEST(Fluid2D, InstalledStepsThroughTheSchedule) {
    engine::ecs::World world;
    engine::register_engine_systems(world);
    engine::install_fluid_2d(world);
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -10.f, 0.f};
    const engine::ecs::Entity entity = add_fluid(world);
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({1.f, 2.f}));
    step(world);
    EXPECT_LT(world.get<engine::Fluid2D>(entity).positions()[0].y, 2.f);
}

TEST(Fluid2D, SecondInstallAddsNoSecondSystem) {
    engine::JobSystem jobs(2);
    engine::ecs::World once = fluid_world();
    engine::ecs::World twice = fluid_world();
    engine::install_fluid_2d(twice, &jobs);
    engine::install_fluid_2d(twice);
    const engine::ecs::Entity a = add_fluid(once);
    const engine::ecs::Entity b = add_fluid(twice);
    ASSERT_TRUE(once.get<engine::Fluid2D>(a).spawn({1.f, 2.f}));
    ASSERT_TRUE(twice.get<engine::Fluid2D>(b).spawn({1.f, 2.f}));
    step(once, 5);
    step(twice, 5);
    EXPECT_EQ(once.get<engine::Fluid2D>(a).positions()[0], twice.get<engine::Fluid2D>(b).positions()[0]);
    EXPECT_EQ(once.get<engine::Fluid2D>(a).velocities()[0], twice.get<engine::Fluid2D>(b).velocities()[0]);
}

TEST(Fluid2D, InstallBeforeEngineSystemsStillStepsOnce) {
    engine::ecs::World early;
    engine::install_fluid_2d(early);
    engine::register_engine_systems(early);
    early.ctx<engine::Physics2DConfig>().gravity = {0.f, -10.f, 0.f};
    engine::ecs::World late = fluid_world();
    const engine::ecs::Entity a = add_fluid(early);
    const engine::ecs::Entity b = add_fluid(late);
    ASSERT_TRUE(early.get<engine::Fluid2D>(a).spawn({1.f, 2.f}));
    ASSERT_TRUE(late.get<engine::Fluid2D>(b).spawn({1.f, 2.f}));
    step(early, 5);
    step(late, 5);
    EXPECT_LT(early.get<engine::Fluid2D>(a).positions()[0].y, 2.f);
    EXPECT_EQ(early.get<engine::Fluid2D>(a).positions()[0], late.get<engine::Fluid2D>(b).positions()[0]);
}

TEST(Fluid2D, StepsAfterPhysicsWhereverInstalled) {
    // Control: a fluid step registered ahead of the engine's physics misses the paddle's move.
    engine::ecs::World before;
    before.add_system(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Physics,
                      [](engine::ecs::World& w) { engine::run_fluid_2d(w); });
    engine::register_engine_systems(before);
    const engine::ecs::Entity control = order_probe(before);
    step(before);
    EXPECT_EQ(before.get<engine::Fluid2D>(control).positions()[0], glm::vec2(1.f, 0.5f));

    engine::ecs::World early;
    engine::install_fluid_2d(early);
    engine::register_engine_systems(early);
    const engine::ecs::Entity a = order_probe(early);
    step(early);
    EXPECT_NE(early.get<engine::Fluid2D>(a).positions()[0], glm::vec2(1.f, 0.5f));

    engine::ecs::World late;
    engine::register_engine_systems(late);
    engine::install_fluid_2d(late);
    const engine::ecs::Entity b = order_probe(late);
    step(late);
    EXPECT_NE(late.get<engine::Fluid2D>(b).positions()[0], glm::vec2(1.f, 0.5f));
}

// LQ-59: swept boundaries. A particle that moves much farther than a wall is thick in one substep stops at the wall
// instead of crossing it.

namespace {

// Particles in a column at x = 0, `spacing` apart (beyond the kernel, so they do not interact), all moving at
// `velocity`.
void fire_column(engine::Fluid2D& fluid, glm::vec2 velocity, int count = 9, float spacing = 0.4f) {
    for (int i = 0; i < count; ++i) {
        ASSERT_TRUE(fluid.spawn({0.f, (static_cast<float>(i) - static_cast<float>(count / 2)) * spacing}, velocity));
    }
}

float max_x(const engine::Fluid2D& fluid) {
    float highest = -1.0e9f;
    for (const glm::vec2 p : fluid.positions()) {
        highest = std::max(highest, p.x);
    }
    return highest;
}

}

TEST(Fluid2DSweep, FastParticlesDoNotCrossAHalfCellSdfWall) {
    // A wall half a cell (0.05) thick at x in [0.975, 1.025], over the sample column x = 1 (a sampled field holds a
    // wall that thin only across a sample); particles at 60 units/s move 0.5 a substep, ten times that.
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    const std::array<glm::vec2, 4> wall{glm::vec2{0.975f, -3.f}, glm::vec2{1.025f, -3.f}, glm::vec2{1.025f, 3.f},
                                        glm::vec2{0.975f, 3.f}};
    (void)field.fill_polygon(wall);
    ASSERT_LT(field.sample({1.f, 0.f}), 0.f) << "the wall must be solid at its middle";

    for (const float speed : {20.f, 60.f, 200.f}) {
        engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
        (void)add_terrain(world, field);
        const engine::ecs::Entity entity = add_fluid(world);
        fire_column(world.get<engine::Fluid2D>(entity), {speed, speed * 0.1f});
        step(world, 30);
        const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
        ASSERT_TRUE(all_finite(fluid));
        EXPECT_LT(max_x(fluid), 1.f) << "a particle crossed the wall at speed " << speed;
    }
}

TEST(Fluid2DSweep, FastParticlesDoNotCrossAThinRotatedBox) {
    for (const float speed : {30.f, 120.f}) {
        engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
        const engine::ecs::Entity wall = world.create();
        world.emplace<engine::Transform>(wall, engine::Transform{.position = {1.5f, 0.f, 0.f},
                                                                 .rotation = engine::rotation_z(0.3f)});
        // Long enough that nothing slides off its end within the test.
        world.emplace<engine::BoxCollider2D>(wall, engine::BoxCollider2D{.size = {0.02f, 100.f, 1.f}});
        const engine::ecs::Entity entity = add_fluid(world);
        fire_column(world.get<engine::Fluid2D>(entity), {speed, 0.f});
        step(world, 30);
        // Every particle stays on the near side of the wall's center line.
        const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
        const engine::SinCos turn = engine::det_sin_cos(0.3f);
        for (const glm::vec2 p : fluid.positions()) {
            const float side = (p.x - 1.5f) * turn.cos + p.y * turn.sin;
            EXPECT_LT(side, 0.f) << "speed " << speed << " particle at " << p.x << ", " << p.y;
        }
    }
}

TEST(Fluid2DSweep, FastParticleDoesNotCrossASmallCircle) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity peg = world.create();
    world.emplace<engine::Transform>(peg, engine::Transform{.position = {2.f, 0.f, 0.f}});
    world.emplace<engine::CircleCollider2D>(peg, engine::CircleCollider2D{.radius = 0.03f});
    const engine::ecs::Entity entity = add_fluid(world);
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({0.f, 0.f}, {150.f, 0.f}));
    step(world, 10);
    EXPECT_LT(world.get<engine::Fluid2D>(entity).positions()[0].x, 2.f);
}

TEST(Fluid2DSweep, ParticleTouchingAWallIsStoppedWhenFiredIntoIt) {
    // Resting on the wall's face (one radius off), then fired straight into it.
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    const std::array<glm::vec2, 4> wall{glm::vec2{0.975f, -3.f}, glm::vec2{1.025f, -3.f}, glm::vec2{1.025f, 3.f},
                                        glm::vec2{0.975f, 3.f}};
    (void)field.fill_polygon(wall);
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    (void)add_terrain(world, field);
    const engine::ecs::Entity entity = add_fluid(world);
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({0.975f - kRadius, 0.f}, {80.f, 0.f}));
    step(world, 20);
    EXPECT_LT(world.get<engine::Fluid2D>(entity).positions()[0].x, 1.f);
}

TEST(Fluid2DSweep, SweptParticleKeepsSlidingAlongTheWall) {
    // A glancing hit loses its normal speed, not its tangential one (friction 0).
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    const std::array<glm::vec2, 4> wall{glm::vec2{0.975f, -30.f}, glm::vec2{1.025f, -30.f}, glm::vec2{1.025f, 30.f},
                                        glm::vec2{0.975f, 30.f}};
    (void)field.fill_polygon(wall);
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    (void)add_terrain(world, field);
    const engine::ecs::Entity entity =
            add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .viscosity = 0.f, .friction = 0.f});
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({0.f, 0.f}, {60.f, 10.f}));
    step(world, 5);
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    EXPECT_LT(fluid.positions()[0].x, 1.f);
    EXPECT_GT(fluid.velocities()[0].y, 5.f);
    EXPECT_LT(std::abs(fluid.velocities()[0].x), 1.f);
}

// LQ-57: PolygonCollider2D and CompoundCollider2D bound a fluid.

namespace {

engine::CompoundShape2D quad(glm::vec2 lo, glm::vec2 hi) {
    engine::CompoundShape2D shape;
    shape.count = 4;
    shape.vertices[0] = lo;
    shape.vertices[1] = {hi.x, lo.y};
    shape.vertices[2] = hi;
    shape.vertices[3] = {lo.x, hi.y};
    return shape;
}

// A tank of compound parts: a floor of 0.25-unit tiles, walls of tiles, and a ramp triangle on the floor.
engine::CompoundCollider2D compound_tank(float width, float height, uint32_t layer = 1) {
    engine::CompoundCollider2D compound{.layer = layer};
    for (float x = -0.5f; x < width + 0.5f; x += 0.25f) {
        compound.shapes.push_back(quad({x, -0.5f}, {x + 0.25f, 0.f}));
    }
    for (float y = 0.f; y < height; y += 0.25f) {
        compound.shapes.push_back(quad({-0.5f, y}, {0.f, y + 0.25f}));
        compound.shapes.push_back(quad({width, y}, {width + 0.5f, y + 0.25f}));
    }
    engine::CompoundShape2D ramp;
    ramp.count = 3;
    ramp.vertices[0] = {width * 0.5f, 0.f};
    ramp.vertices[1] = {width, 0.f};
    ramp.vertices[2] = {width, 0.8f};
    compound.shapes.push_back(ramp);
    return compound;
}

// Signed distance from `p` to the convex CCW polygon (negative inside).
float polygon_distance(glm::vec2 p, std::span<const glm::vec2> v) {
    float inside = -1.0e9f;
    float outside = 1.0e9f;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const glm::vec2 a = v[i];
        const glm::vec2 b = v[(i + 1) % v.size()];
        const glm::vec2 e = b - a;
        const glm::vec2 n = glm::normalize(glm::vec2{e.y, -e.x});
        inside = std::max(inside, glm::dot(p - a, n));
        const float t = std::clamp(glm::dot(p - a, e) / glm::dot(e, e), 0.f, 1.f);
        outside = std::min(outside, glm::length(p - (a + e * t)));
    }
    return inside <= 0.f ? inside : outside;
}

float min_compound_distance(const engine::Fluid2D& fluid, const engine::CompoundCollider2D& compound) {
    float lowest = 1.0e9f;
    for (const glm::vec2 p : fluid.positions()) {
        for (const engine::CompoundShape2D& shape : compound.shapes) {
            lowest = std::min(lowest, polygon_distance(p, std::span<const glm::vec2>{shape.vertices, shape.count}));
        }
    }
    return lowest;
}

struct CompoundDam {
    std::uint64_t hash = 0;
    float clearance = 0.f;
    std::size_t count = 0;
};

// A dam break in the compound tank. With `physics` the engine systems run (static parts come from the 2D solver's
// cache); without, run_fluid_2d alone reads the components.
CompoundDam compound_dam(bool physics, engine::JobSystem* jobs = nullptr) {
    engine::ecs::World world;
    if (physics) {
        engine::register_engine_systems(world, engine::EngineSystemDeps{.jobs = jobs});
        engine::install_fluid_2d(world, jobs);
    }
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -10.f, 0.f};
    world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    const engine::ecs::Entity tank = world.create();
    world.emplace<engine::Transform>(tank, engine::Transform{});
    world.emplace<engine::CompoundCollider2D>(tank, compound_tank(4.f, 3.f));
    const engine::ecs::Entity entity = add_fluid(world);
    (void)world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {1.2f, 2.f});
    for (int i = 0; i < 240; ++i) {
        if (physics) {
            world.run(engine::ecs::Schedule::Fixed);
        } else {
            engine::run_fluid_2d(world, jobs);
        }
    }
    if (physics) {
        // The static parts came from the solver's cache, not from the views.
        const engine::PhysicsBroadphase2D* cache = world.try_ctx<engine::PhysicsBroadphase2D>();
        EXPECT_TRUE(cache != nullptr && !cache->dirty);
    }
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    return {fluid.state_hash(), min_compound_distance(fluid, world.get<engine::CompoundCollider2D>(tank)),
            fluid.count()};
}

}

TEST(Fluid2DPolygon, DamBreakInACompoundTankStaysOutsideEveryPart) {
    const CompoundDam dam = compound_dam(true);
    EXPECT_EQ(dam.count, 240u);
    EXPECT_GT(dam.clearance, kRadius * 0.5f) << "a particle is inside or deep against a compound part";
}

TEST(Fluid2DPolygon, StaticCacheAndComponentViewsGiveTheSameFluid) {
    // The engine path reads the static parts from the solver's AABB tree; run_fluid_2d alone reads the components.
    const CompoundDam cached = compound_dam(true);
    const CompoundDam viewed = compound_dam(false);
    EXPECT_EQ(cached.hash, viewed.hash);
    engine::JobSystem jobs{3};
    EXPECT_EQ(compound_dam(true, &jobs).hash, cached.hash);
}

TEST(Fluid2DPolygon, StaticPolygonColliderBoundsIt) {
    engine::ecs::World world = fluid_world();
    const engine::ecs::Entity slab = world.create();
    world.emplace<engine::Transform>(
            slab, engine::Transform{.position = {1.f, 0.f, 0.f}, .rotation = engine::rotation_z(0.2f)});
    engine::PolygonCollider2D polygon{};
    polygon.count = 4;
    polygon.vertices[0] = {-2.f, -0.5f};
    polygon.vertices[1] = {2.f, -0.5f};
    polygon.vertices[2] = {2.f, 0.f};
    polygon.vertices[3] = {-2.f, 0.f};
    world.emplace<engine::PolygonCollider2D>(slab, polygon);
    const engine::ecs::Entity entity = add_fluid(world);
    (void)world.get<engine::Fluid2D>(entity).spawn_box({0.5f, 0.5f}, {1.5f, 1.f});
    step(world, 120);
    // The slab tilts 0.2 rad: the fluid rests on it or runs off its end, never through it.
    const engine::SinCos turn = engine::det_sin_cos(0.2f);
    for (const glm::vec2 p : world.get<engine::Fluid2D>(entity).positions()) {
        const glm::vec2 local{(p.x - 1.f) * turn.cos + p.y * turn.sin, -(p.x - 1.f) * turn.sin + p.y * turn.cos};
        if (std::abs(local.x) < 1.9f) {
            EXPECT_GT(local.y, kRadius * 0.5f) << p.x << ", " << p.y;
        }
    }
}

TEST(Fluid2DPolygon, DynamicPolygonPushesMasslessFluidOneWay) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity body = world.create();
    world.emplace<engine::Transform>(body, engine::Transform{.position = {-0.5f, 0.5f, 0.f}});
    world.emplace<engine::RigidBody2D>(
            body, engine::RigidBody2D{.velocity = {2.f, 0.f, 0.f}, .type = engine::BodyType::Dynamic});
    engine::PolygonCollider2D polygon{};
    polygon.count = 3;
    polygon.vertices[0] = {-0.2f, -0.2f};
    polygon.vertices[1] = {0.2f, 0.f};
    polygon.vertices[2] = {-0.2f, 0.2f};
    world.emplace<engine::PolygonCollider2D>(body, polygon);
    const engine::ecs::Entity entity = add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .density = 0.f});
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({0.5f, 0.5f}));
    step(world, 60);
    EXPECT_GT(world.get<engine::Fluid2D>(entity).positions()[0].x, 0.6f) << "the wedge pushed the particle";
    EXPECT_FLOAT_EQ(world.get<engine::RigidBody2D>(body).velocity.x, 2.f) << "and the fluid did not push back";
}

TEST(Fluid2DPolygon, CompoundLayerMaskAndDynamicParentRules) {
    // A compound tank on layer 2 does not bound a fluid whose mask leaves layer 2 out.
    {
        engine::ecs::World world = fluid_world();
        const engine::ecs::Entity tank = world.create();
        world.emplace<engine::Transform>(tank, engine::Transform{});
        world.emplace<engine::CompoundCollider2D>(tank, compound_tank(2.f, 1.f, 2u));
        const engine::ecs::Entity entity =
                add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .mask = 0xFFFFFFFFu & ~2u});
        ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({1.f, 0.5f}));
        step(world, 60);
        EXPECT_LT(world.get<engine::Fluid2D>(entity).positions()[0].y, -1.f) << "fell through the masked tank";
    }
    // A compound under a Dynamic body is ignored, as the 2D solver ignores it.
    {
        engine::ecs::World world = fluid_world();
        const engine::ecs::Entity tank = world.create();
        world.emplace<engine::Transform>(tank, engine::Transform{});
        world.emplace<engine::RigidBody2D>(
                tank, engine::RigidBody2D{.type = engine::BodyType::Dynamic, .gravity_scale = 0.f});
        world.emplace<engine::CompoundCollider2D>(tank, compound_tank(2.f, 1.f));
        const engine::ecs::Entity entity = add_fluid(world);
        ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({1.f, 0.5f}));
        step(world, 60);
        EXPECT_LT(world.get<engine::Fluid2D>(entity).positions()[0].y, -1.f);
    }
}

TEST(Fluid2DPolygon, FastParticlesDoNotCrossAThinCompoundPart) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity wall = world.create();
    world.emplace<engine::Transform>(wall, engine::Transform{});
    engine::CompoundCollider2D compound;
    compound.shapes.push_back(quad({1.f, -50.f}, {1.02f, 50.f}));
    world.emplace<engine::CompoundCollider2D>(wall, compound);
    const engine::ecs::Entity entity = add_fluid(world);
    fire_column(world.get<engine::Fluid2D>(entity), {120.f, 20.f});
    step(world, 30);
    EXPECT_LT(max_x(world.get<engine::Fluid2D>(entity)), 1.f);
}

// LQ-56: two-way coupling. The fluid pushes Dynamic bodies back: buoyancy and drag.

namespace {

struct Pool {
    engine::ecs::World world;
    engine::ecs::Entity fluid{};
    engine::ecs::Entity body{};
};

// A pool 1.2 deep in a 3-unit SDF tank, settled, then a `size` box of `density` (mass per area) dropped with its
// center at `drop_y`. With `angle` the box starts tilted.
Pool pool_with_box(glm::vec2 size, float density, float drop_y, engine::JobSystem* jobs = nullptr, float angle = 0.f) {
    Pool pool{fluid_world(jobs)};
    (void)add_terrain(pool.world, tank_field(3.f, 3.f));
    pool.fluid = add_fluid(pool.world);
    (void)pool.world.get<engine::Fluid2D>(pool.fluid).spawn_box({0.f, 0.f}, {3.f, 1.2f});
    step(pool.world, 120);
    pool.body = pool.world.create();
    pool.world.emplace<engine::Transform>(
            pool.body, engine::Transform{.position = {1.5f, drop_y, 0.f}, .rotation = engine::rotation_z(angle)});
    pool.world.emplace<engine::BoxCollider2D>(pool.body, engine::BoxCollider2D{.size = {size.x, size.y, 1.f}});
    pool.world.emplace<engine::RigidBody2D>(
            pool.body, engine::RigidBody2D{.type = engine::BodyType::Dynamic, .mass = size.x * size.y * density});
    return pool;
}

// The highest particle in the column under x in [x0, x1]: where the water surface is.
float surface_near(const engine::Fluid2D& fluid, float x0, float x1) {
    float top = -1.0e9f;
    for (const glm::vec2 p : fluid.positions()) {
        if (p.x >= x0 && p.x <= x1) {
            top = std::max(top, p.y);
        }
    }
    return top;
}

}

TEST(Fluid2DCoupling, LightBoxFloatsAtAStableWaterline) {
    Pool pool = pool_with_box({0.4f, 0.4f}, 0.4f, 1.6f);
    step(pool.world, 300);
    float lowest = 1.0e9f;
    float highest = -1.0e9f;
    float fastest = 0.f;
    for (int i = 0; i < 120; ++i) {
        step(pool.world, 1);
        const float y = pool.world.get<engine::Transform>(pool.body).position.y;
        lowest = std::min(lowest, y);
        highest = std::max(highest, y);
        fastest = std::max(fastest, glm::length(glm::vec2{pool.world.get<engine::RigidBody2D>(pool.body).velocity}));
    }
    const float surface = surface_near(pool.world.get<engine::Fluid2D>(pool.fluid), 0.f, 0.8f);
    // Floating: its center within its own half height of the surface, not on the floor, not flying off.
    EXPECT_GT(lowest, surface - 0.25f) << "surface " << surface;
    EXPECT_LT(highest, surface + 0.25f) << "surface " << surface;
    EXPECT_LT(highest - lowest, 0.06f) << "the waterline is not stable";
    EXPECT_LT(fastest, 0.3f);
}

TEST(Fluid2DCoupling, HeavyBoxSinks) {
    Pool pool = pool_with_box({0.4f, 0.4f}, 3.f, 1.6f);
    step(pool.world, 300);
    EXPECT_LT(pool.world.get<engine::Transform>(pool.body).position.y, 0.35f) << "a dense box ends on the floor";
}

TEST(Fluid2DCoupling, LighterBoxesFloatHigher) {
    Pool light = pool_with_box({0.4f, 0.4f}, 0.25f, 1.6f);
    Pool medium = pool_with_box({0.4f, 0.4f}, 0.6f, 1.6f);
    step(light.world, 300);
    step(medium.world, 300);
    EXPECT_GT(light.world.get<engine::Transform>(light.body).position.y,
              medium.world.get<engine::Transform>(medium.body).position.y + 0.03f);
}

TEST(Fluid2DCoupling, DroppedBodySplashesAndSlows) {
    Pool wet = pool_with_box({0.4f, 0.4f}, 0.8f, 2.4f);
    // The same drop with a massless fluid: the body falls as if dry until it reaches the floor.
    Pool dry = pool_with_box({0.4f, 0.4f}, 0.8f, 2.4f);
    dry.world.get<engine::Fluid2D>(dry.fluid).params.density = 0.f;
    float splash = 0.f;
    for (int i = 0; i < 30; ++i) {
        step(wet.world, 1);
        step(dry.world, 1);
        for (const glm::vec2 v : wet.world.get<engine::Fluid2D>(wet.fluid).velocities()) {
            splash = std::max(splash, v.y);
        }
    }
    const float wet_y = wet.world.get<engine::Transform>(wet.body).position.y;
    const float dry_y = dry.world.get<engine::Transform>(dry.body).position.y;
    EXPECT_GT(wet_y, dry_y + 0.05f) << "the water slows the body";
    EXPECT_GT(splash, 0.5f) << "and throws water up";
    EXPECT_TRUE(all_finite(wet.world.get<engine::Fluid2D>(wet.fluid)));
}

TEST(Fluid2DCoupling, TiltedPlankRightsItself) {
    Pool pool = pool_with_box({1.2f, 0.2f}, 0.4f, 1.4f, nullptr, 0.6f);
    step(pool.world, 420);
    const float angle = engine::angle_z(pool.world.get<engine::Transform>(pool.body).rotation);
    EXPECT_LT(std::abs(angle), 0.25f) << "buoyancy torque turns a floating plank flat";
}

TEST(Fluid2DCoupling, NothingExplodesAtRest) {
    Pool pool = pool_with_box({0.5f, 0.3f}, 0.5f, 1.3f);
    step(pool.world, 600);
    const engine::Fluid2D& fluid = pool.world.get<engine::Fluid2D>(pool.fluid);
    ASSERT_TRUE(all_finite(fluid));
    EXPECT_LT(kinetic_energy(fluid) / static_cast<float>(fluid.count()), 0.01f);
    const engine::RigidBody2D& body = pool.world.get<engine::RigidBody2D>(pool.body);
    EXPECT_LT(glm::length(glm::vec2{body.velocity}), 0.3f);
    EXPECT_LT(std::abs(body.angular_velocity), 0.5f);
}

TEST(Fluid2DCoupling, BitIdenticalAcrossWorkerCounts) {
    Pool serial = pool_with_box({0.4f, 0.3f}, 0.5f, 2.f, nullptr, 0.3f);
    engine::JobSystem jobs{3};
    Pool parallel = pool_with_box({0.4f, 0.3f}, 0.5f, 2.f, &jobs, 0.3f);
    step(serial.world, 120);
    step(parallel.world, 120);
    EXPECT_EQ(engine::physics_state_hash_2d(serial.world), engine::physics_state_hash_2d(parallel.world));
    EXPECT_EQ(serial.world.get<engine::Fluid2D>(serial.fluid).state_hash(),
              parallel.world.get<engine::Fluid2D>(parallel.fluid).state_hash());
}

TEST(Fluid2DCoupling, SleepingBodyWakesWhenWaterHitsIt) {
    // A box asleep on a dry floor wakes when a wave reaches it.
    engine::ecs::World world = fluid_world();
    (void)add_terrain(world, tank_field(4.f, 3.f));
    const engine::ecs::Entity box = world.create();
    world.emplace<engine::Transform>(box, engine::Transform{.position = {3.5f, 0.2f, 0.f}});
    world.emplace<engine::BoxCollider2D>(box, engine::BoxCollider2D{.size = {0.4f, 0.4f, 1.f}});
    world.emplace<engine::RigidBody2D>(box, engine::RigidBody2D{.type = engine::BodyType::Dynamic, .mass = 0.05f});
    step(world, 120);
    ASSERT_TRUE(world.get<engine::RigidBody2D>(box).sleeping);
    const engine::ecs::Entity fluid = add_fluid(world);
    (void)world.get<engine::Fluid2D>(fluid).spawn_box({0.f, 0.f}, {1.5f, 2.f});
    bool woke = false;
    for (int i = 0; i < 120 && !woke; ++i) {
        step(world, 1);
        woke = !world.get<engine::RigidBody2D>(box).sleeping;
    }
    EXPECT_TRUE(woke);
}

TEST(Fluid2DCoupling, VeryLightBodyBobsButStaysInThePool) {
    // Far lighter than the water (2 percent): the explicit reaction is capped by the speed of the water that pushes
    // it, so the box bobs at the surface instead of being thrown out.
    Pool pool = pool_with_box({0.4f, 0.4f}, 0.02f, 1.6f);
    for (int i = 0; i < 600; ++i) {
        step(pool.world, 1);
        const glm::vec3 p = pool.world.get<engine::Transform>(pool.body).position;
        ASSERT_TRUE(std::isfinite(p.x) && std::isfinite(p.y));
        ASSERT_GT(p.x, 0.f);
        ASSERT_LT(p.x, 3.f);
        ASSERT_GT(p.y, 0.5f) << "tick " << i;
        ASSERT_LT(p.y, 4.f) << "tick " << i;
    }
    EXPECT_TRUE(all_finite(pool.world.get<engine::Fluid2D>(pool.fluid)));
}

// LQ-60: sources, drains, sensors and region queries.

namespace {

engine::FluidRegion2D box_region(glm::vec2 center, glm::vec2 half) {
    return engine::FluidRegion2D{.shape = engine::FluidRegion2D::Shape::Box, .center = center, .half_extents = half};
}

engine::FluidRegion2D circle_region(glm::vec2 center, float radius) {
    return engine::FluidRegion2D{.shape = engine::FluidRegion2D::Shape::Circle, .center = center, .radius = radius};
}

// Events sent since the last call (the cursor keeps the reader from seeing an event on two frames).
std::vector<engine::FluidSensorEvent2D> sensor_events(engine::ecs::World& world,
                                                      engine::ecs::EventCursor<engine::FluidSensorEvent2D>& cursor) {
    std::vector<engine::FluidSensorEvent2D> out;
    for (const engine::FluidSensorEvent2D& event : engine::ecs::EventReader<engine::FluidSensorEvent2D>{world, cursor}) {
        out.push_back(event);
    }
    return out;
}

}

TEST(Fluid2DSources, EmitAtTheRate) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity entity = add_fluid(world);
    engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    fluid.sources.push_back(engine::FluidSource2D{.region = box_region({0.f, 0.f}, {2.f, 2.f}), .rate = 30.f});
    step(world, 120);
    EXPECT_EQ(world.get<engine::Fluid2D>(entity).count(), 60u) << "30 per second for 2 seconds";

    // A rate that is not a whole number per tick carries the remainder: 10 per second is 1/6 a tick.
    world.get<engine::Fluid2D>(entity).sources[0].rate = 10.f;
    world.get<engine::Fluid2D>(entity).clear();
    step(world, 600);
    const std::size_t count = world.get<engine::Fluid2D>(entity).count();
    EXPECT_GE(count, 99u);
    EXPECT_LE(count, 101u);
    EXPECT_EQ(world.get<engine::Fluid2D>(entity).sources[0].emitted, 60u + count);
}

TEST(Fluid2DSources, EmitInsideTheRegionWithTheVelocity) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity entity = add_fluid(world, engine::Fluid2DParams{.radius = 0.01f});
    world.get<engine::Fluid2D>(entity).sources.push_back(engine::FluidSource2D{
            .region = circle_region({3.f, 1.f}, 0.5f), .rate = 600.f, .velocity = {0.f, 0.f}});
    world.get<engine::Fluid2D>(entity).sources.push_back(engine::FluidSource2D{
            .region = box_region({-3.f, 1.f}, {0.5f, 0.25f}), .rate = 600.f, .velocity = {0.f, 0.f}});
    step(world, 1);
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    ASSERT_EQ(fluid.count(), 20u);
    for (const glm::vec2 p : fluid.positions()) {
        // One tick of the solver may separate close particles by a little; the regions are far apart.
        if (p.x > 0.f) {
            EXPECT_LE(glm::length(p - glm::vec2{3.f, 1.f}), 0.5f + 0.02f);
        } else {
            EXPECT_LE(std::abs(p.x + 3.f), 0.5f + 0.02f);
            EXPECT_LE(std::abs(p.y - 1.f), 0.25f + 0.02f);
        }
    }

    // A fast stream keeps its velocity.
    engine::ecs::World stream = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity jet = add_fluid(stream);
    stream.get<engine::Fluid2D>(jet).sources.push_back(engine::FluidSource2D{
            .region = box_region({0.f, 0.f}, {0.01f, 0.5f}), .rate = 60.f, .velocity = {4.f, 0.f}});
    step(stream, 1);
    ASSERT_EQ(stream.get<engine::Fluid2D>(jet).count(), 1u);
    EXPECT_FLOAT_EQ(stream.get<engine::Fluid2D>(jet).velocities()[0].x, 4.f);
}

TEST(Fluid2DSources, StopAtMaxParticlesAndResumeWhenDrained) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity entity =
            add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .max_particles = 50});
    engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    fluid.sources.push_back(engine::FluidSource2D{
            .region = box_region({0.f, 0.f}, {1.f, 1.f}), .rate = 600.f, .velocity = {3.f, 0.f}});
    step(world, 30);
    EXPECT_EQ(world.get<engine::Fluid2D>(entity).count(), 50u);
    const std::uint64_t emitted = world.get<engine::Fluid2D>(entity).sources[0].emitted;
    EXPECT_EQ(emitted, 50u) << "what does not fit is dropped, not queued";

    // A drain to the right takes the particles as they leave: emission refills the free room.
    world.get<engine::Fluid2D>(entity).drains.push_back(
            engine::FluidDrain2D{.region = box_region({4.f, 0.f}, {1.f, 3.f})});
    step(world, 120);
    EXPECT_GT(world.get<engine::Fluid2D>(entity).drains[0].drained, 0u);
    EXPECT_GT(world.get<engine::Fluid2D>(entity).sources[0].emitted, emitted);
    EXPECT_LE(world.get<engine::Fluid2D>(entity).count(), 50u);
}

TEST(Fluid2DDrains, RemoveWhatEntersAndKeepTheOrder) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity entity = add_fluid(world);
    engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    // Particles far apart, each tagged by its y; the even ones move right into the drain, the odd ones stay.
    for (int i = 0; i < 10; ++i) {
        const float speed = i % 2 == 0 ? 3.f : 0.f;
        ASSERT_TRUE(fluid.spawn({0.f, static_cast<float>(i)}, {speed, 0.f}));
    }
    fluid.drains.push_back(engine::FluidDrain2D{.region = box_region({2.f, 4.5f}, {0.5f, 10.f})});
    step(world, 60);
    const engine::Fluid2D& after = world.get<engine::Fluid2D>(entity);
    ASSERT_EQ(after.count(), 5u);
    EXPECT_EQ(after.drains[0].drained, 5u);
    for (std::size_t k = 0; k < after.count(); ++k) {
        EXPECT_FLOAT_EQ(after.positions()[k].y, static_cast<float>(2 * k + 1)) << "the survivors keep their order";
    }
    // A disabled drain takes nothing.
    world.get<engine::Fluid2D>(entity).drains[0].enabled = false;
    ASSERT_TRUE(world.get<engine::Fluid2D>(entity).spawn({2.f, 0.f}));
    step(world, 1);
    EXPECT_EQ(world.get<engine::Fluid2D>(entity).count(), 6u);
}

TEST(Fluid2DQueries, RegionCountsMatchBruteForce) {
    engine::ecs::World world = fluid_world();
    (void)add_terrain(world, tank_field());
    const engine::ecs::Entity entity = add_fluid(world);
    (void)world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {1.5f, 2.f});
    step(world, 45);
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);

    auto brute = [&](auto&& inside) {
        engine::FluidQuery2D out;
        glm::vec2 sum{0.f};
        for (std::size_t i = 0; i < fluid.count(); ++i) {
            if (inside(fluid.positions()[i])) {
                ++out.count;
                sum += fluid.velocities()[i];
            }
        }
        out.volume = static_cast<float>(out.count) * 4.f * kRadius * kRadius;
        out.average_velocity = out.count > 0 ? sum / static_cast<float>(out.count) : glm::vec2{0.f};
        return out;
    };
    auto expect_same = [](const engine::FluidQuery2D& a, const engine::FluidQuery2D& b) {
        EXPECT_EQ(a.count, b.count);
        EXPECT_NEAR(a.volume, b.volume, 1e-5f);
        EXPECT_NEAR(a.average_velocity.x, b.average_velocity.x, 1e-4f);
        EXPECT_NEAR(a.average_velocity.y, b.average_velocity.y, 1e-4f);
    };
    for (const engine::FluidRegion2D region :
         {box_region({0.7f, 0.4f}, {0.5f, 0.3f}), box_region({2.f, 0.2f}, {2.f, 0.2f}), circle_region({1.f, 1.f}, 0.6f),
          circle_region({3.5f, 0.1f}, 0.3f), box_region({-5.f, -5.f}, {0.1f, 0.1f}), box_region({2.f, 2.f}, {50.f, 50.f})}) {
        expect_same(fluid.query(region), brute([&](glm::vec2 p) { return region.contains(p); }));
    }
    // A concave polygon (an L), counter-clockwise, and the same reversed.
    std::vector<glm::vec2> l_shape{{0.f, 0.f}, {2.f, 0.f}, {2.f, 0.4f}, {0.6f, 0.4f}, {0.6f, 1.5f}, {0.f, 1.5f}};
    auto in_l = [](glm::vec2 p) {
        return (p.x >= 0.f && p.x <= 2.f && p.y >= 0.f && p.y <= 0.4f) ||
               (p.x >= 0.f && p.x <= 0.6f && p.y >= 0.f && p.y <= 1.5f);
    };
    const engine::FluidQuery2D l = fluid.query(std::span<const glm::vec2>{l_shape});
    const engine::FluidQuery2D expected = brute(in_l);
    EXPECT_NEAR(static_cast<float>(l.count), static_cast<float>(expected.count), 2.f) << "edges may split a point";
    EXPECT_GT(l.count, 0u);
    std::reverse(l_shape.begin(), l_shape.end());
    EXPECT_EQ(fluid.query(std::span<const glm::vec2>{l_shape}).count, l.count);
}

TEST(Fluid2DQueries, IsWetMatchesBruteForce) {
    engine::ecs::World world = fluid_world();
    (void)add_terrain(world, tank_field());
    const engine::ecs::Entity entity = add_fluid(world);
    (void)world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {1.f, 1.f});
    step(world, 30);
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    const float reach = 2.f * kRadius;
    for (int iy = -2; iy < 30; ++iy) {
        for (int ix = -2; ix < 50; ++ix) {
            const glm::vec2 point{static_cast<float>(ix) * 0.0937f, static_cast<float>(iy) * 0.0611f};
            bool wet = false;
            for (const glm::vec2 p : fluid.positions()) {
                wet = wet || glm::dot(p - point, p - point) <= reach * reach;
            }
            EXPECT_EQ(fluid.is_wet(point), wet) << point.x << ", " << point.y;
        }
    }
    // The grid follows the particles: after a step and after a spawn.
    step(world, 1);
    engine::Fluid2D& moved = world.get<engine::Fluid2D>(entity);
    EXPECT_FALSE(moved.is_wet({8.f, 8.f}));
    ASSERT_TRUE(moved.spawn({8.f, 8.f}));
    EXPECT_TRUE(moved.is_wet({8.f, 8.f}));
}

TEST(Fluid2DSensors, FireOnceWhenFilledAndOnceWhenDrained) {
    auto run = [](engine::JobSystem* jobs) {
        engine::ecs::World world = fluid_world(jobs);
        (void)add_terrain(world, tank_field(2.f, 2.f));
        const engine::ecs::Entity entity = add_fluid(world);
        engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
        fluid.sources.push_back(engine::FluidSource2D{
                .region = box_region({1.f, 1.6f}, {0.8f, 0.1f}), .rate = 400.f, .velocity = {0.f, -1.f}});
        fluid.sensors.push_back(engine::FluidSensor2D{.region = box_region({1.f, 0.25f}, {0.9f, 0.2f}),
                                                      .threshold = 0.7f});
        std::vector<std::pair<int, engine::FluidSensorEvent2D>> log;
        engine::ecs::EventCursor<engine::FluidSensorEvent2D> cursor;
        for (int tick = 0; tick < 600; ++tick) {
            if (tick == 300) {
                // Stop the tap, open a drain over the whole tank floor.
                world.get<engine::Fluid2D>(entity).sources[0].enabled = false;
                world.get<engine::Fluid2D>(entity).drains.push_back(
                        engine::FluidDrain2D{.region = box_region({1.f, 0.f}, {1.f, 0.15f})});
            }
            step(world, 1);
            for (const engine::FluidSensorEvent2D& event : sensor_events(world, cursor)) {
                log.emplace_back(tick, event);
            }
            world.flush_events();
        }
        return log;
    };
    const auto log = run(nullptr);
    ASSERT_EQ(log.size(), 2u);
    EXPECT_TRUE(log[0].second.full);
    EXPECT_GE(log[0].second.fill, 0.7f);
    EXPECT_LT(log[0].first, 300);
    EXPECT_FALSE(log[1].second.full);
    EXPECT_GT(log[1].first, 300);
    EXPECT_EQ(log[0].second.sensor, 0u);

    engine::JobSystem jobs{3};
    const auto parallel = run(&jobs);
    ASSERT_EQ(parallel.size(), log.size());
    for (std::size_t i = 0; i < log.size(); ++i) {
        EXPECT_EQ(parallel[i].first, log[i].first) << "the events fire on the same tick for any worker count";
        EXPECT_EQ(parallel[i].second, log[i].second);
    }
}

TEST(Fluid2DSources, SerializerRoundTripsSourcesDrainsAndSensors) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    engine::Fluid2D fluid;
    fluid.sources.push_back(engine::FluidSource2D{
            .region = circle_region({1.f, 2.f}, 0.3f), .rate = 12.5f, .velocity = {0.f, -2.f}, .enabled = false});
    fluid.drains.push_back(engine::FluidDrain2D{.region = box_region({5.f, 0.f}, {1.f, 0.5f})});
    fluid.sensors.push_back(engine::FluidSensor2D{
            .region = box_region({0.f, 0.f}, {2.f, 1.f}), .threshold = 0.4f, .hysteresis = 0.1f});
    fluid.sources[0].emitted = 99;  // state: not saved
    world.emplace<engine::Fluid2D>(entity, fluid);
    const std::string saved = engine::ecs::save_world_to_toml(world);
    engine::ecs::World loaded_world;
    auto loaded = engine::ecs::load_world_from_toml(loaded_world, saved);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    const engine::Fluid2D* c = nullptr;
    loaded_world.view<engine::Fluid2D>().each([&](const engine::Fluid2D& f) { c = &f; });
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->sources.size(), 1u);
    EXPECT_EQ(c->sources[0].region.shape, engine::FluidRegion2D::Shape::Circle);
    EXPECT_FLOAT_EQ(c->sources[0].region.center.y, 2.f);
    EXPECT_FLOAT_EQ(c->sources[0].region.radius, 0.3f);
    EXPECT_FLOAT_EQ(c->sources[0].rate, 12.5f);
    EXPECT_FLOAT_EQ(c->sources[0].velocity.y, -2.f);
    EXPECT_FALSE(c->sources[0].enabled);
    EXPECT_EQ(c->sources[0].emitted, 0u);
    ASSERT_EQ(c->drains.size(), 1u);
    EXPECT_FLOAT_EQ(c->drains[0].region.half_extents.y, 0.5f);
    ASSERT_EQ(c->sensors.size(), 1u);
    EXPECT_FLOAT_EQ(c->sensors[0].threshold, 0.4f);
    EXPECT_FLOAT_EQ(c->sensors[0].hysteresis, 0.1f);
}

TEST(Fluid2DSources, StateHashCoversTheSourceCarry) {
    engine::Fluid2D a;
    a.sources.push_back(engine::FluidSource2D{.rate = 10.f});
    engine::Fluid2D b = a;
    EXPECT_EQ(a.state_hash(), b.state_hash());
    b.sources[0].carry = 0.5f;
    EXPECT_NE(a.state_hash(), b.state_hash());
}

// LQ-58: cohesion (surface tension) and vorticity confinement.

namespace {

float farthest_from_centroid(const engine::Fluid2D& fluid) {
    glm::vec2 centroid{0.f};
    for (const glm::vec2 p : fluid.positions()) {
        centroid += p;
    }
    centroid /= static_cast<float>(fluid.count());
    float far = 0.f;
    for (const glm::vec2 p : fluid.positions()) {
        far = std::max(far, glm::length(p - centroid));
    }
    return far;
}

float angular_momentum(const engine::Fluid2D& fluid) {
    float sum = 0.f;
    for (std::size_t i = 0; i < fluid.count(); ++i) {
        const glm::vec2 p = fluid.positions()[i];
        const glm::vec2 v = fluid.velocities()[i];
        sum += p.x * v.y - p.y * v.x;
    }
    return sum;
}

// A disc of fluid turning rigidly at 3 rad/s in zero gravity.
engine::ecs::Entity spinning_disc(engine::ecs::World& world, float vorticity) {
    const engine::ecs::Entity entity =
            add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .vorticity = vorticity});
    engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    for (int y = -12; y <= 12; ++y) {
        for (int x = -12; x <= 12; ++x) {
            const glm::vec2 p{static_cast<float>(x) * 0.1f, static_cast<float>(y) * 0.1f};
            if (glm::length(p) <= 1.2f) {
                (void)fluid.spawn(p, glm::vec2{-p.y, p.x} * 3.f);
            }
        }
    }
    return entity;
}

}

TEST(Fluid2DCohesion, FallingBlobStaysTogetherAsADrop) {
    auto fall = [](float cohesion) {
        engine::ecs::World world = fluid_world();
        const engine::ecs::Entity entity =
                add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .cohesion = cohesion});
        (void)world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {0.8f, 0.8f});
        step(world, 60);
        EXPECT_EQ(world.get<engine::Fluid2D>(entity).count(), 64u);
        EXPECT_TRUE(all_finite(world.get<engine::Fluid2D>(entity)));
        return farthest_from_centroid(world.get<engine::Fluid2D>(entity));
    };
    // The 8 x 8 block starts with its corners 0.5 from its center. Without cohesion its free surface frays as it
    // falls; with it the drop keeps together.
    const float loose = fall(0.f);
    const float tight = fall(64.f);
    EXPECT_GT(loose, 1.2f);
    EXPECT_LT(tight, 0.6f);
}

TEST(Fluid2DCohesion, ZeroGravityBlockRoundsWithoutGainingEnergy) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity entity =
            add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .tensile_strength = 0.f, .cohesion = 4.f});
    (void)world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {1.6f, 1.6f});
    const float corners = farthest_from_centroid(world.get<engine::Fluid2D>(entity));
    step(world, 120);
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    EXPECT_LT(farthest_from_centroid(fluid), corners * 0.95f) << "surface tension pulls the corners in";
    EXPECT_LT(kinetic_energy(fluid) / static_cast<float>(fluid.count()), 1.e-3f);
}

TEST(Fluid2DCohesion, RestingPoolStillSettles) {
    for (const float cohesion : {4.f, 16.f}) {
        engine::ecs::World world = fluid_world();
        (void)add_terrain(world, tank_field());
        const engine::ecs::Entity entity =
                add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .cohesion = cohesion});
        (void)world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {1.5f, 1.5f});
        step(world, 600);
        const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
        ASSERT_TRUE(all_finite(fluid));
        EXPECT_LT(kinetic_energy(fluid) / static_cast<float>(fluid.count()), 1.e-3f) << "cohesion " << cohesion;
    }
}

TEST(Fluid2DVorticity, ConfinementKeepsASwirlTurning) {
    auto spin = [](float vorticity) {
        engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
        const engine::ecs::Entity entity = spinning_disc(world, vorticity);
        const float start = angular_momentum(world.get<engine::Fluid2D>(entity));
        step(world, 120);
        EXPECT_TRUE(all_finite(world.get<engine::Fluid2D>(entity)));
        return angular_momentum(world.get<engine::Fluid2D>(entity)) / start;
    };
    const float damped = spin(0.f);
    const float confined = spin(0.05f);
    EXPECT_LT(damped, 0.99f) << "viscosity and the solver damp the swirl";
    EXPECT_GT(confined, damped + 0.01f) << "confinement puts some of it back";
    EXPECT_LT(confined, 1.01f) << "and adds no swirl of its own at a safe strength";
}

TEST(Fluid2DVorticity, RestingPoolStillSettles) {
    engine::ecs::World world = fluid_world();
    (void)add_terrain(world, tank_field());
    const engine::ecs::Entity entity = add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .vorticity = 0.1f});
    (void)world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {1.5f, 1.5f});
    step(world, 600);
    const engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    EXPECT_LT(kinetic_energy(fluid) / static_cast<float>(fluid.count()), 1.e-3f);
}

TEST(Fluid2DCohesion, BitIdenticalAcrossWorkerCountsWithCohesionAndVorticity) {
    auto run = [](engine::JobSystem* jobs) {
        engine::ecs::World world = fluid_world(jobs);
        (void)add_terrain(world, tank_field());
        const engine::ecs::Entity entity = add_fluid(
                world, engine::Fluid2DParams{.radius = kRadius, .cohesion = 8.f, .vorticity = 0.05f});
        (void)world.get<engine::Fluid2D>(entity).spawn_box({0.f, 0.f}, {1.5f, 2.f});
        step(world, 90);
        return world.get<engine::Fluid2D>(entity).state_hash();
    };
    engine::JobSystem jobs{3};
    EXPECT_EQ(run(nullptr), run(&jobs));
}

// LQ-62: several fluids. Fluids that touch are solved together, by mass, and report where they meet.

namespace {

float mean_y(const engine::Fluid2D& fluid) {
    float sum = 0.f;
    for (const glm::vec2 p : fluid.positions()) {
        sum += p.y;
    }
    return sum / static_cast<float>(fluid.count());
}

struct TwoFluids {
    engine::ecs::World world;
    engine::ecs::Entity light{};
    engine::ecs::Entity heavy{};
};

// A tank with a light fluid (`light_density`) spawned under a heavy one (density 1): the light one must rise.
TwoFluids layered_tank(float light_density, engine::JobSystem* jobs = nullptr) {
    TwoFluids out{fluid_world(jobs)};
    (void)add_terrain(out.world, tank_field(2.f, 3.f));
    out.light = add_fluid(out.world, engine::Fluid2DParams{.radius = kRadius, .density = light_density});
    out.heavy = add_fluid(out.world, engine::Fluid2DParams{.radius = kRadius, .density = 1.f});
    (void)out.world.get<engine::Fluid2D>(out.light).spawn_box({0.f, 0.f}, {2.f, 0.6f});
    (void)out.world.get<engine::Fluid2D>(out.heavy).spawn_box({0.f, 0.6f}, {2.f, 1.2f});
    return out;
}

std::vector<engine::FluidContactEvent2D> contact_events(engine::ecs::World& world,
                                                        engine::ecs::EventCursor<engine::FluidContactEvent2D>& cursor) {
    std::vector<engine::FluidContactEvent2D> out;
    for (const engine::FluidContactEvent2D& event : engine::ecs::EventReader<engine::FluidContactEvent2D>{world, cursor}) {
        out.push_back(event);
    }
    return out;
}

}

TEST(Fluid2DMultiple, LighterFluidPouredOnHeavierStaysOnTop) {
    // Oil (0.4) dropped as a blob into a settled pool of water (1): it plunges in, comes back up and floats.
    engine::ecs::World world = fluid_world();
    (void)add_terrain(world, tank_field(2.f, 3.f));
    const engine::ecs::Entity water = add_fluid(world);
    const engine::ecs::Entity oil = add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .density = 0.4f});
    (void)world.get<engine::Fluid2D>(water).spawn_box({0.f, 0.f}, {2.f, 0.8f});
    step(world, 120);
    (void)world.get<engine::Fluid2D>(oil).spawn_box({0.6f, 1.6f}, {1.4f, 2.f});
    step(world, 900);
    const engine::Fluid2D& light = world.get<engine::Fluid2D>(oil);
    const engine::Fluid2D& heavy = world.get<engine::Fluid2D>(water);
    ASSERT_TRUE(all_finite(light));
    ASSERT_TRUE(all_finite(heavy));
    std::vector<float> heavy_y;
    for (const glm::vec2 p : heavy.positions()) {
        heavy_y.push_back(p.y);
    }
    std::sort(heavy_y.begin(), heavy_y.end());
    const float surface = heavy_y[heavy_y.size() * 9 / 10];
    std::size_t above = 0;
    for (const glm::vec2 p : light.positions()) {
        above += p.y > surface ? 1u : 0u;
    }
    EXPECT_GT(static_cast<float>(above), 0.95f * static_cast<float>(light.count())) << "the oil floats on the water";
    EXPECT_LT((kinetic_energy(light) + kinetic_energy(heavy)) / static_cast<float>(light.count() + heavy.count()), 1.e-3f)
            << "and comes to rest";
}

TEST(Fluid2DMultiple, HeavierBlobSinksToTheBottom) {
    engine::ecs::World world = fluid_world();
    (void)add_terrain(world, tank_field(2.f, 3.f));
    const engine::ecs::Entity water = add_fluid(world);
    const engine::ecs::Entity mud = add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .density = 2.5f});
    (void)world.get<engine::Fluid2D>(water).spawn_box({0.f, 0.f}, {2.f, 0.8f});
    step(world, 120);
    (void)world.get<engine::Fluid2D>(mud).spawn_box({0.6f, 1.6f}, {1.4f, 2.f});
    step(world, 900);
    EXPECT_LT(mean_y(world.get<engine::Fluid2D>(mud)), mean_y(world.get<engine::Fluid2D>(water)) - 0.2f);
}

TEST(Fluid2DMultiple, InvertedLayersTurnOver) {
    // A light layer under a heavy one is unstable: the heavy one sinks through it. A resting PBF fluid jams like a
    // granular pile, so the turn-over stops short of two clean layers; it still inverts the order.
    TwoFluids tank = layered_tank(0.4f);
    const float light_start = mean_y(tank.world.get<engine::Fluid2D>(tank.light));
    step(tank.world, 900);
    const engine::Fluid2D& light = tank.world.get<engine::Fluid2D>(tank.light);
    const engine::Fluid2D& heavy = tank.world.get<engine::Fluid2D>(tank.heavy);
    ASSERT_TRUE(all_finite(light));
    EXPECT_GT(mean_y(light), light_start + 0.2f);
    EXPECT_GT(mean_y(light), mean_y(heavy) + 0.1f);
    EXPECT_LT((kinetic_energy(light) + kinetic_energy(heavy)) / static_cast<float>(light.count() + heavy.count()), 1.e-3f);
}

TEST(Fluid2DMultiple, EqualDensitiesDoNotSeparate) {
    // Control: the same setup with equal densities keeps the lower fluid lower.
    TwoFluids tank = layered_tank(1.f);
    step(tank.world, 600);
    EXPECT_LT(mean_y(tank.world.get<engine::Fluid2D>(tank.light)), mean_y(tank.world.get<engine::Fluid2D>(tank.heavy)));
}

TEST(Fluid2DMultiple, FluidsApartStepAsIfAlone) {
    auto alone = [](glm::vec2 offset) {
        engine::ecs::World world = fluid_world();
        (void)add_terrain(world, floor_field());
        const engine::ecs::Entity entity = add_fluid(world);
        (void)world.get<engine::Fluid2D>(entity).spawn_box(offset, offset + glm::vec2{0.6f, 0.6f});
        step(world, 60);
        return world.get<engine::Fluid2D>(entity).state_hash();
    };
    engine::ecs::World world = fluid_world();
    (void)add_terrain(world, floor_field());
    const engine::ecs::Entity a = add_fluid(world);
    const engine::ecs::Entity b = add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .density = 0.3f});
    (void)world.get<engine::Fluid2D>(a).spawn_box({-2.f, 0.f}, {-1.4f, 0.6f});
    (void)world.get<engine::Fluid2D>(b).spawn_box({4.f, 0.f}, {4.6f, 0.6f});
    step(world, 60);
    EXPECT_EQ(world.get<engine::Fluid2D>(a).state_hash(), alone({-2.f, 0.f}));
    // The density only weighs against other fluids (and bodies): alone, b steps as a fluid of density 1 would.
    EXPECT_EQ(world.get<engine::Fluid2D>(b).state_hash(), alone({4.f, 0.f}));
}

TEST(Fluid2DMultiple, MasksThatExcludeEachOtherPassThrough) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity a =
            add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .layer = 1u << 1, .mask = ~(1u << 2)});
    const engine::ecs::Entity b =
            add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .layer = 1u << 2, .mask = 0xFFFFFFFFu});
    ASSERT_TRUE(world.get<engine::Fluid2D>(a).spawn({0.f, 0.f}, {1.f, 0.f}));
    ASSERT_TRUE(world.get<engine::Fluid2D>(b).spawn({1.f, 0.f}, {-1.f, 0.f}));
    engine::ecs::EventCursor<engine::FluidContactEvent2D> cursor;
    std::size_t events = 0;
    for (int i = 0; i < 60; ++i) {
        step(world, 1);
        events += contact_events(world, cursor).size();
        world.flush_events();
    }
    // They crossed: a's particle ends right of b's.
    EXPECT_GT(world.get<engine::Fluid2D>(a).positions()[0].x, world.get<engine::Fluid2D>(b).positions()[0].x);
    EXPECT_EQ(events, 0u);
}

TEST(Fluid2DMultiple, ContactEventsReportWhereFluidsMeet) {
    auto run = [](engine::JobSystem* jobs) {
        engine::ecs::World world = fluid_world(jobs);
        (void)add_terrain(world, tank_field(3.f, 2.f));
        const engine::ecs::Entity water = add_fluid(world);
        const engine::ecs::Entity lava = add_fluid(world, engine::Fluid2DParams{.radius = kRadius, .density = 2.f});
        (void)world.get<engine::Fluid2D>(water).spawn_box({0.f, 0.f}, {1.f, 1.f});
        (void)world.get<engine::Fluid2D>(lava).spawn_box({2.f, 0.f}, {3.f, 1.f});
        engine::ecs::EventCursor<engine::FluidContactEvent2D> cursor;
        std::vector<std::pair<int, engine::FluidContactEvent2D>> log;
        for (int tick = 0; tick < 120; ++tick) {
            step(world, 1);
            for (const engine::FluidContactEvent2D& event : contact_events(world, cursor)) {
                log.emplace_back(tick, event);
            }
            world.flush_events();
        }
        return std::tuple{log, water, lava};
    };
    const auto [log, water, lava] = run(nullptr);
    ASSERT_FALSE(log.empty()) << "the two spreading pools meet";
    EXPECT_GT(log.front().first, 0) << "not at the start: they begin a unit apart";
    for (const auto& [tick, event] : log) {
        EXPECT_EQ(event.a, water);
        EXPECT_EQ(event.b, lava);
        EXPECT_GT(event.count, 0u);
        EXPECT_GE(event.center.x, event.min.x);
        EXPECT_LE(event.center.x, event.max.x);
        EXPECT_GT(event.center.x, 0.5f);
        EXPECT_LT(event.center.x, 2.5f);
    }
    engine::JobSystem jobs{3};
    const auto [parallel, w2, l2] = run(&jobs);
    ASSERT_EQ(parallel.size(), log.size());
    for (std::size_t i = 0; i < log.size(); ++i) {
        EXPECT_EQ(parallel[i].first, log[i].first);
        EXPECT_EQ(parallel[i].second, log[i].second) << "the same events for any worker count";
    }
}

TEST(Fluid2DMultiple, BitIdenticalAcrossWorkerCounts) {
    TwoFluids serial = layered_tank(0.5f);
    engine::JobSystem jobs{3};
    TwoFluids parallel = layered_tank(0.5f, &jobs);
    step(serial.world, 120);
    step(parallel.world, 120);
    EXPECT_EQ(serial.world.get<engine::Fluid2D>(serial.light).state_hash(),
              parallel.world.get<engine::Fluid2D>(parallel.light).state_hash());
    EXPECT_EQ(serial.world.get<engine::Fluid2D>(serial.heavy).state_hash(),
              parallel.world.get<engine::Fluid2D>(parallel.heavy).state_hash());
}

// LQ-61: fluid state in saves, and pools authored with FluidBox2D.

namespace {

// A tank with a dam break, a tap, a drain and a sensor, stepped a while: every kind of fluid state is non-trivial.
engine::ecs::Entity busy_fluid(engine::ecs::World& world) {
    (void)add_terrain(world, tank_field());
    const engine::ecs::Entity entity = add_fluid(world);
    engine::Fluid2D& fluid = world.get<engine::Fluid2D>(entity);
    (void)fluid.spawn_box({0.f, 0.f}, {1.f, 1.5f});
    fluid.sources.push_back(engine::FluidSource2D{
            .region = {.center = {3.f, 2.5f}, .half_extents = {0.3f, 0.05f}}, .rate = 37.f, .velocity = {0.f, -1.f}});
    fluid.drains.push_back(engine::FluidDrain2D{.region = {.center = {3.9f, 0.1f}, .half_extents = {0.1f, 0.1f}}});
    fluid.sensors.push_back(engine::FluidSensor2D{.region = {.center = {1.f, 0.2f}, .half_extents = {1.f, 0.2f}}});
    fluid.save_particles = true;
    return entity;
}

const engine::Fluid2D* only_fluid(engine::ecs::World& world) {
    const engine::Fluid2D* found = nullptr;
    world.view<engine::Fluid2D>().each([&](const engine::Fluid2D& fluid) { found = &fluid; });
    return found;
}

}

TEST(Fluid2DSave, SavedParticlesLoadAsTheSameStateAndKeepStepping) {
    engine::ecs::World world = fluid_world();
    const engine::ecs::Entity entity = busy_fluid(world);
    step(world, 47);
    const std::string saved = engine::ecs::save_world_to_toml(world);
    EXPECT_NE(saved.find("state"), std::string::npos);

    engine::ecs::World loaded = fluid_world();
    auto result = engine::ecs::load_world_from_toml(loaded, saved);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    const engine::Fluid2D* copy = only_fluid(loaded);
    ASSERT_NE(copy, nullptr);
    const engine::Fluid2D& original = world.get<engine::Fluid2D>(entity);
    ASSERT_EQ(copy->count(), original.count());
    EXPECT_EQ(copy->state_hash(), original.state_hash());
    EXPECT_EQ(copy->sources[0].emitted, original.sources[0].emitted);
    EXPECT_EQ(copy->drains[0].drained, original.drains[0].drained);
    EXPECT_EQ(copy->sensors[0].full, original.sensors[0].full);
    for (std::size_t i = 0; i < original.count(); ++i) {
        ASSERT_EQ(std::bit_cast<std::uint32_t>(copy->densities()[i]), std::bit_cast<std::uint32_t>(original.densities()[i]));
    }

    // Both go on the same: the save holds everything the next steps read.
    step(world, 30);
    step(loaded, 30);
    EXPECT_EQ(only_fluid(loaded)->state_hash(), world.get<engine::Fluid2D>(entity).state_hash());
}

TEST(Fluid2DSave, WithoutTheFlagNoStateIsWritten) {
    engine::ecs::World world = fluid_world();
    const engine::ecs::Entity entity = busy_fluid(world);
    world.get<engine::Fluid2D>(entity).save_particles = false;
    step(world, 5);
    const std::string saved = engine::ecs::save_world_to_toml(world);
    EXPECT_EQ(saved.find("state ="), std::string::npos);
    engine::ecs::World loaded;
    ASSERT_TRUE(engine::ecs::load_world_from_toml(loaded, saved).has_value());
    EXPECT_EQ(only_fluid(loaded)->count(), 0u);
}

TEST(Fluid2DSave, LoadStateRefusesWhatIsNotAState) {
    engine::Fluid2D fluid{engine::Fluid2DParams{.max_particles = 10}};
    ASSERT_TRUE(fluid.spawn({1.f, 2.f}, {3.f, 4.f}));
    const std::vector<std::uint8_t> good = fluid.save_state();
    const std::uint64_t hash = fluid.state_hash();

    engine::Fluid2D target{engine::Fluid2DParams{.max_particles = 10}};
    std::vector<std::uint8_t> bad = good;
    bad.pop_back();
    EXPECT_FALSE(target.load_state(bad)) << "truncated";
    bad = good;
    bad[0] ^= 0xFF;
    EXPECT_FALSE(target.load_state(bad)) << "wrong magic";
    bad = good;
    bad.push_back(0);
    EXPECT_FALSE(target.load_state(bad)) << "trailing bytes";
    bad = good;
    const std::uint32_t nan = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    for (int k = 0; k < 4; ++k) {
        bad[16 + k] = static_cast<std::uint8_t>(nan >> (8 * k));  // the first position's x
    }
    EXPECT_FALSE(target.load_state(bad)) << "non-finite";
    engine::Fluid2D small{engine::Fluid2DParams{.max_particles = 0}};
    EXPECT_FALSE(small.load_state(good)) << "over max_particles";
    EXPECT_EQ(target.count(), 0u) << "a refused state changes nothing";

    EXPECT_TRUE(target.load_state(good));
    EXPECT_EQ(target.state_hash(), hash);
}

TEST(Fluid2DBox, FillsItsParentFluidOnceAndRoundTrips) {
    engine::ecs::World world = fluid_world(nullptr, {0.f, 0.f, 0.f});
    const engine::ecs::Entity fluid = add_fluid(world);
    const engine::ecs::Entity box = world.create();
    world.emplace<engine::Transform>(box, engine::Transform{.position = {1.f, 1.f, 0.f}});
    world.emplace<engine::FluidBox2D>(box, engine::FluidBox2D{.size = {0.4f, 0.4f}, .velocity = {0.5f, 0.f}});
    // A box without a fluid parent waits.
    step(world, 1);
    EXPECT_FALSE(world.get<engine::FluidBox2D>(box).filled);
    EXPECT_EQ(world.get<engine::Fluid2D>(fluid).count(), 0u);

    engine::ecs::set_parent(world, box, fluid);
    step(world, 1);
    EXPECT_TRUE(world.get<engine::FluidBox2D>(box).filled);
    const engine::Fluid2D& filled = world.get<engine::Fluid2D>(fluid);
    ASSERT_EQ(filled.count(), 16u) << "a 0.4 box holds 4 x 4 at the rest spacing";
    for (const glm::vec2 p : filled.positions()) {
        EXPECT_NEAR(p.x, 1.f, 0.3f);
        EXPECT_NEAR(p.y, 1.f, 0.3f);
    }
    step(world, 10);
    EXPECT_EQ(world.get<engine::Fluid2D>(fluid).count(), 16u) << "once";

    const std::string saved = engine::ecs::save_world_to_toml(world);
    engine::ecs::World loaded;
    ASSERT_TRUE(engine::ecs::load_world_from_toml(loaded, saved).has_value());
    bool found = false;
    loaded.view<engine::FluidBox2D>().each([&](const engine::FluidBox2D& b) {
        found = true;
        EXPECT_TRUE(b.filled);
        EXPECT_FLOAT_EQ(b.size.x, 0.4f);
        EXPECT_FLOAT_EQ(b.velocity.x, 0.5f);
    });
    EXPECT_TRUE(found);
}
