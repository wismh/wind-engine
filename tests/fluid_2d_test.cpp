#include <gtest/gtest.h>

#include <engine/core/det_math.h>
#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>

#include "ecs/fluid_2d_internal.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
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

TEST(Fluid2D, DynamicBodyPushesFluidOneWay) {
    auto simulate = [](bool with_fluid, bool check) {
        engine::ecs::World world = fluid_world();
        (void)add_terrain(world, floor_field());
        const engine::ecs::Entity ball = world.create();
        world.emplace<engine::Transform>(ball, engine::Transform{.position = {2.f, 1.5f, 0.f}});
        world.emplace<engine::CircleCollider2D>(ball, engine::CircleCollider2D{.radius = 0.25f});
        world.emplace<engine::RigidBody2D>(ball, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
        const engine::ecs::Entity entity = add_fluid(world);
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
