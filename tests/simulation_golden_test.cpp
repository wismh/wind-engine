#include <gtest/gtest.h>

#include <engine/core/det_math.h>
#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include "core/float_mode.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

// LQ-63: golden state hashes of fixed simulation scenes. Determinism is the default (Principles.md, Deterministic
// simulation): every supported target and configuration must step these scenes to the same bits. A hash that
// changes fails here. If the change in simulation is intended, run engine_tests, take the printed hash, update the
// constant and say in the commit why the simulation changed. A hash that differs between targets is a bug.
//
// Targets these hashes were checked on: docs/tech/architecture/Principles.md (Deterministic simulation).

namespace {

// --- Golden values. Update only on purpose, with the reason in the commit.
constexpr std::uint64_t kGoldenStack2D = 0xe8d28498f5232f35ull;
constexpr std::uint64_t kGoldenTerrain2D = 0x79d87f3c8c4d5979ull;
constexpr std::uint64_t kGoldenTerrainField = 0xcf18cbc3cbefb34eull;
constexpr std::uint64_t kGoldenDamBreakFluid = 0xb2576ea4bcce9772ull;  // LQ-64: boundaries in content order
constexpr std::uint64_t kGoldenDamBreakBodies = 0xe14ec2bcaa61dc8full;  // LQ-64: boundaries in content order
constexpr std::uint64_t kGoldenStack3D = 0xd0aa1e5806fe43faull;
constexpr std::uint64_t kGoldenSleepingPool = 0xbdd890d4456c49b7ull;  // LQ-38: a pool that falls asleep, then is woken

// The gap above |value|: one float ulp (the smallest denormal at zero).
double ulp_of(float value) {
    const float magnitude = std::fabs(value);
    return static_cast<double>(std::nextafter(magnitude, INFINITY)) - static_cast<double>(magnitude);
}

std::string hex(std::uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%016" PRIx64 "ull", value);
    return buffer;
}

void step(engine::ecs::World& world, int ticks) {
    world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    for (int i = 0; i < ticks; ++i) {
        world.run(engine::ecs::Schedule::Fixed);
    }
}

engine::ecs::World physics_world(engine::JobSystem* jobs) {
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.jobs = jobs});
    return world;
}

engine::ecs::Entity add_static_box(engine::ecs::World& world, glm::vec2 center, glm::vec2 size, float angle = 0.f) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = glm::vec3{center, 0.f},
                                                     .rotation = engine::rotation_z(angle)});
    world.emplace<engine::BoxCollider2D>(
            entity, engine::BoxCollider2D{.size = glm::vec3{size, 1.f}, .friction = 0.6f, .restitution = 0.05f});
    return entity;
}

engine::ecs::Entity add_box_2d(engine::ecs::World& world, glm::vec2 center, glm::vec2 size, float angle,
        float angular_velocity = 0.f) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = glm::vec3{center, 0.f},
                                                     .rotation = engine::rotation_z(angle)});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{.type = engine::BodyType::Dynamic,
                                                       .mass = size.x * size.y,
                                                       .angular_velocity = angular_velocity});
    world.emplace<engine::BoxCollider2D>(
            entity, engine::BoxCollider2D{.size = glm::vec3{size, 1.f}, .friction = 0.5f, .restitution = 0.1f});
    return entity;
}

engine::ecs::Entity add_circle_2d(engine::ecs::World& world, glm::vec2 center, float radius, glm::vec2 velocity) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = glm::vec3{center, 0.f}});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{.velocity = glm::vec3{velocity, 0.f},
                                                       .type = engine::BodyType::Dynamic,
                                                       .mass = radius * radius * 3.f});
    world.emplace<engine::CircleCollider2D>(
            entity, engine::CircleCollider2D{.radius = radius, .friction = 0.4f, .restitution = 0.2f});
    return entity;
}

engine::ecs::Entity add_pentagon_2d(engine::ecs::World& world, glm::vec2 center, float radius, float angle) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = glm::vec3{center, 0.f},
                                                     .rotation = engine::rotation_z(angle)});
    world.emplace<engine::RigidBody2D>(
            entity, engine::RigidBody2D{.type = engine::BodyType::Dynamic, .mass = radius * radius * 2.4f});
    engine::PolygonCollider2D polygon{};
    polygon.count = 5;
    polygon.friction = 0.5f;
    polygon.restitution = 0.1f;
    for (int i = 0; i < 5; ++i) {
        // Vertices from the engine's own trigonometry, so the scene is the same bits on every target.
        const engine::SinCos corner = engine::det_sin_cos(static_cast<float>(i) * 1.2566370614f);
        polygon.vertices[i] = glm::vec2{corner.cos, corner.sin} * radius;
    }
    world.emplace<engine::PolygonCollider2D>(entity, polygon);
    return entity;
}

// A box stack, a rotated pentagon pile and spinning boxes on a floor, a static compound ramp beside them.
void build_stack_2d(engine::ecs::World& world) {
    world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -20.f, 0.f};
    add_static_box(world, {0.f, -1.f}, {40.f, 2.f});
    add_static_box(world, {-12.f, 4.f}, {1.f, 10.f}, 0.15f);
    add_static_box(world, {12.f, 4.f}, {1.f, 10.f}, -0.15f);

    const engine::ecs::Entity ramp = world.create();
    world.emplace<engine::Transform>(ramp, engine::Transform{.position = glm::vec3{6.f, 0.f, 0.f}});
    engine::CompoundCollider2D compound{.friction = 0.4f};
    compound.shapes.push_back(engine::box_shape_2d({0.f, 0.5f}, {4.f, 1.f}, 0.35f));
    compound.shapes.push_back(engine::box_shape_2d({2.5f, 1.2f}, {1.f, 2.4f}));
    world.emplace<engine::CompoundCollider2D>(ramp, std::move(compound));

    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 3; ++column) {
            const float x = -4.f + static_cast<float>(column) * 1.05f + (row % 2 == 0 ? 0.f : 0.3f);
            add_box_2d(world, {x, 0.5f + static_cast<float>(row) * 1.02f}, {1.f, 1.f}, 0.01f * static_cast<float>(row));
        }
    }
    for (int i = 0; i < 6; ++i) {
        add_pentagon_2d(world, {5.f + static_cast<float>(i) * 0.4f, 4.f + static_cast<float>(i) * 1.3f}, 0.45f,
                0.7f * static_cast<float>(i));
    }
    for (int i = 0; i < 4; ++i) {
        add_box_2d(world, {-9.f + static_cast<float>(i) * 1.5f, 6.f + static_cast<float>(i)}, {0.8f, 0.4f},
                0.5f * static_cast<float>(i), 3.f - static_cast<float>(i) * 2.f);
    }
    for (int i = 0; i < 5; ++i) {
        add_circle_2d(world, {-6.f + static_cast<float>(i) * 2.f, 12.f}, 0.3f + 0.05f * static_cast<float>(i),
                {1.5f - static_cast<float>(i) * 0.7f, 0.f});
    }
}

// Rolling SDF hills, carved twice while bodies rest on them.
struct TerrainScene {
    engine::ecs::Entity terrain{};
};

TerrainScene build_terrain_2d(engine::ecs::World& world) {
    world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -15.f, 0.f};
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = 0.25f, .band = 1.f}};
    const std::array<glm::vec2, 4> ground{glm::vec2{-20.f, -6.f}, glm::vec2{20.f, -6.f}, glm::vec2{20.f, 0.f},
                                          glm::vec2{-20.f, 0.f}};
    (void)field.fill_polygon(ground);
    for (int i = 0; i < 6; ++i) {
        (void)field.fill_circle({-15.f + static_cast<float>(i) * 6.f, -0.5f}, 2.f + static_cast<float>(i % 3) * 0.5f);
    }
    (void)field.carve_circle({3.f, 0.f}, 2.5f);

    TerrainScene scene;
    scene.terrain = world.create();
    world.emplace<engine::Transform>(scene.terrain, engine::Transform{.rotation = engine::rotation_z(0.05f)});
    world.emplace<engine::SdfCollider2D>(scene.terrain, engine::SdfCollider2D{.field = std::move(field), .friction = 0.6f});

    for (int i = 0; i < 10; ++i) {
        const float x = -14.f + static_cast<float>(i) * 3.f;
        if (i % 3 == 0) {
            add_circle_2d(world, {x, 4.f}, 0.4f, {0.5f, 0.f});
        } else if (i % 3 == 1) {
            add_box_2d(world, {x, 4.5f}, {0.9f, 0.6f}, 0.2f * static_cast<float>(i));
        } else {
            add_pentagon_2d(world, {x, 5.f}, 0.5f, 0.3f * static_cast<float>(i));
        }
    }
    return scene;
}

void carve(engine::ecs::World& world, engine::ecs::Entity terrain, glm::vec2 center, float radius) {
    (void)world.get<engine::SdfCollider2D>(terrain).field.carve_circle(center, radius);
    engine::mark_static_colliders_dirty_2d(world, terrain);
}

struct TerrainResult {
    std::uint64_t physics = 0;
    std::uint64_t field = 0;
};

TerrainResult run_terrain_2d(engine::JobSystem* jobs) {
    engine::ecs::World world = physics_world(jobs);
    const TerrainScene scene = build_terrain_2d(world);
    step(world, 60);
    carve(world, scene.terrain, {-8.f, 0.5f}, 1.8f);
    step(world, 60);
    carve(world, scene.terrain, {7.f, 1.f}, 2.2f);
    step(world, 120);
    return {engine::physics_state_hash_2d(world), world.get<engine::SdfCollider2D>(scene.terrain).field.hash()};
}

std::uint64_t run_stack_2d(engine::JobSystem* jobs) {
    engine::ecs::World world = physics_world(jobs);
    build_stack_2d(world);
    step(world, 240);
    return engine::physics_state_hash_2d(world);
}

// A water column released in a box tank, with dynamic boxes and circles falling into it.
struct DamBreakResult {
    std::uint64_t fluid = 0;
    std::uint64_t bodies = 0;
    float lowest = 0.f;  // the lowest particle: the tank holds the water
};

DamBreakResult run_dam_break(engine::JobSystem* jobs) {
    engine::ecs::World world = physics_world(jobs);
    engine::install_fluid_2d(world, jobs);
    world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -10.f, 0.f};
    add_static_box(world, {2.f, -0.25f}, {5.f, 0.5f});
    add_static_box(world, {-0.25f, 1.5f}, {0.5f, 4.f});
    add_static_box(world, {4.25f, 1.5f}, {0.5f, 4.f});
    add_static_box(world, {2.6f, 0.3f}, {0.6f, 0.6f}, 0.6f);

    const engine::ecs::Entity water = world.create();
    engine::Fluid2D fluid{engine::Fluid2DParams{.radius = 0.05f}};
    (void)fluid.spawn_box({0.f, 0.f}, {1.2f, 2.f});
    world.emplace<engine::Fluid2D>(water, std::move(fluid));

    add_box_2d(world, {3.f, 2.5f}, {0.4f, 0.3f}, 0.4f, 1.f);
    add_circle_2d(world, {1.8f, 3.f}, 0.2f, {0.3f, 0.f});
    step(world, 120);
    const engine::Fluid2D& result = world.get<engine::Fluid2D>(water);
    float lowest = 1.e9f;
    for (const glm::vec2 position : result.positions()) {
        lowest = std::min(lowest, position.y);
    }
    return {result.state_hash(), engine::physics_state_hash_2d(world), lowest};
}

struct SleepingPoolResult {
    std::uint64_t fluid = 0;
    std::size_t asleep = 0;        // before the wall goes
    std::size_t asleep_after = 0;  // at the end
};

// LQ-38: the dam break's tank with sleeping on. The pool comes to rest and falls asleep behind a static box, the box is
// removed (the sleepers next to it wake) and a body drops in. Pins the sleep path (two hashes, listed sleepers, marks,
// holds, wakes) on every target.
SleepingPoolResult run_sleeping_pool(engine::JobSystem* jobs) {
    engine::ecs::World world = physics_world(jobs);
    engine::install_fluid_2d(world, jobs);
    world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -10.f, 0.f};
    add_static_box(world, {2.f, -0.25f}, {5.f, 0.5f});
    add_static_box(world, {-0.25f, 1.5f}, {0.5f, 4.f});
    add_static_box(world, {4.25f, 1.5f}, {0.5f, 4.f});
    const engine::ecs::Entity gate = add_static_box(world, {2.6f, 0.3f}, {0.6f, 0.6f}, 0.6f);

    const engine::ecs::Entity water = world.create();
    engine::Fluid2D fluid{engine::Fluid2DParams{.radius = 0.05f, .sleep = true}};
    (void)fluid.spawn_box({0.f, 0.f}, {1.2f, 1.2f});
    world.emplace<engine::Fluid2D>(water, std::move(fluid));
    step(world, 1500);
    SleepingPoolResult out;
    out.asleep = world.get<engine::Fluid2D>(water).sleeping_count();
    world.destroy(gate);
    add_box_2d(world, {1.f, 1.5f}, {0.4f, 0.3f}, 0.4f, 1.f);
    step(world, 180);
    out.fluid = world.get<engine::Fluid2D>(water).state_hash();
    out.asleep_after = world.get<engine::Fluid2D>(water).sleeping_count();
    return out;
}

engine::ecs::Entity add_box_3d(engine::ecs::World& world, glm::vec3 center, glm::vec3 size, glm::vec3 euler,
        engine::BodyType type) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(
            entity, engine::Transform{.position = center, .rotation = engine::euler_xyz(euler)});
    world.emplace<engine::RigidBody3D>(entity, engine::RigidBody3D{.type = type, .mass = size.x * size.y * size.z});
    world.emplace<engine::BoxCollider3D>(entity, engine::BoxCollider3D{.size = size, .friction = 0.5f});
    return entity;
}

std::uint64_t run_stack_3d(engine::JobSystem* jobs) {
    engine::ecs::World world = physics_world(jobs);
    world.ctx<engine::Physics3DConfig>().gravity = glm::vec3{0.f, -9.81f, 0.f};
    const engine::ecs::Entity floor = world.create();
    world.emplace<engine::Transform>(floor, engine::Transform{.position = glm::vec3{0.f, -0.5f, 0.f}});
    world.emplace<engine::BoxCollider3D>(floor, engine::BoxCollider3D{.size = {30.f, 1.f, 30.f}, .friction = 0.6f});
    for (int level = 0; level < 5; ++level) {
        add_box_3d(world, {0.02f * static_cast<float>(level), 0.5f + static_cast<float>(level) * 1.01f, 0.f},
                {1.f, 1.f, 1.f}, {0.f, 0.1f * static_cast<float>(level), 0.f}, engine::BodyType::Dynamic);
    }
    add_box_3d(world, {3.f, 3.f, 0.5f}, {0.6f, 0.4f, 1.2f}, {0.4f, 0.7f, 0.2f}, engine::BodyType::Dynamic);
    for (int i = 0; i < 3; ++i) {
        const engine::ecs::Entity ball = world.create();
        world.emplace<engine::Transform>(
                ball, engine::Transform{.position = glm::vec3{-2.f + static_cast<float>(i), 4.f + static_cast<float>(i), 0.3f}});
        world.emplace<engine::RigidBody3D>(ball, engine::RigidBody3D{.velocity = {1.f, 0.f, -0.5f},
                                                         .angular_velocity = {0.f, 2.f, 1.f},
                                                         .type = engine::BodyType::Dynamic});
        world.emplace<engine::SphereCollider3D>(ball, engine::SphereCollider3D{.radius = 0.4f, .friction = 0.3f});
    }
    step(world, 240);
    return engine::physics_state_hash_3d(world);
}

}

TEST(SimulationGolden, Stack2DMatchesGoldenForAnyWorkerCount) {
    const std::uint64_t serial = run_stack_2d(nullptr);
    engine::JobSystem jobs{3};
    EXPECT_EQ(run_stack_2d(&jobs), serial);
    EXPECT_EQ(serial, kGoldenStack2D) << "2D stack hash " << hex(serial);
}

TEST(SimulationGolden, SdfTerrainWithCarvesMatchesGolden) {
    const TerrainResult serial = run_terrain_2d(nullptr);
    engine::JobSystem jobs{3};
    const TerrainResult parallel = run_terrain_2d(&jobs);
    EXPECT_EQ(parallel.physics, serial.physics);
    EXPECT_EQ(parallel.field, serial.field);
    EXPECT_EQ(serial.physics, kGoldenTerrain2D) << "terrain physics hash " << hex(serial.physics);
    EXPECT_EQ(serial.field, kGoldenTerrainField) << "terrain field hash " << hex(serial.field);
}

TEST(SimulationGolden, SdfFieldHashCoversCarves) {
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = 0.25f, .band = 1.f}};
    const std::uint64_t empty = field.hash();
    (void)field.fill_circle({0.f, 0.f}, 3.f);
    const std::uint64_t filled = field.hash();
    EXPECT_NE(filled, empty);
    engine::SdfField2D copy = field;
    EXPECT_EQ(copy.hash(), filled);
    (void)copy.carve_circle({1.f, 0.f}, 1.f);
    EXPECT_NE(copy.hash(), filled);
}

TEST(SimulationGolden, DamBreakMatchesGoldenForAnyWorkerCount) {
    const DamBreakResult serial = run_dam_break(nullptr);
    engine::JobSystem jobs{3};
    const DamBreakResult parallel = run_dam_break(&jobs);
    EXPECT_EQ(parallel.fluid, serial.fluid);
    EXPECT_EQ(parallel.bodies, serial.bodies);
    EXPECT_GT(serial.lowest, -0.01f);
    EXPECT_EQ(serial.fluid, kGoldenDamBreakFluid) << "dam break fluid hash " << hex(serial.fluid);
    EXPECT_EQ(serial.bodies, kGoldenDamBreakBodies) << "dam break bodies hash " << hex(serial.bodies);
}

TEST(SimulationGolden, SleepingPoolMatchesGoldenForAnyWorkerCount) {
    const SleepingPoolResult serial = run_sleeping_pool(nullptr);
    engine::JobSystem jobs{3};
    EXPECT_EQ(run_sleeping_pool(&jobs).fluid, serial.fluid);
    EXPECT_GT(serial.asleep, 100u) << "the scene sleeps before the wall goes";
    EXPECT_LT(serial.asleep_after, serial.asleep) << "and the splash woke some of it";
    EXPECT_EQ(serial.fluid, kGoldenSleepingPool) << "sleeping pool fluid hash " << hex(serial.fluid);
}

TEST(SimulationGolden, Stack3DMatchesGoldenForAnyWorkerCount) {
    const std::uint64_t serial = run_stack_3d(nullptr);
    engine::JobSystem jobs{3};
    EXPECT_EQ(run_stack_3d(&jobs), serial);
    EXPECT_EQ(serial, kGoldenStack3D) << "3D stack hash " << hex(serial);
}

TEST(SimulationGolden, StateHashesChangeWithState) {
    engine::Fluid2D fluid;
    const std::uint64_t empty = fluid.state_hash();
    ASSERT_TRUE(fluid.spawn({1.f, 2.f}));
    const std::uint64_t one = fluid.state_hash();
    EXPECT_NE(one, empty);
    engine::Fluid2D other;
    ASSERT_TRUE(other.spawn({1.f, 2.f}, {0.f, 1.e-6f}));
    EXPECT_NE(other.state_hash(), one);

    engine::ecs::World world = physics_world(nullptr);
    const std::uint64_t none = engine::physics_state_hash_2d(world);
    const engine::ecs::Entity body = add_circle_2d(world, {0.f, 0.f}, 0.5f, {0.f, 0.f});
    const std::uint64_t at_rest = engine::physics_state_hash_2d(world);
    EXPECT_NE(at_rest, none);
    world.get<engine::RigidBody2D>(body).velocity.x = 1.f;
    EXPECT_NE(engine::physics_state_hash_2d(world), at_rest);
}

// --- Float mode (rule 5).

TEST(SimulationFloatMode, FixedScheduleRestoresIeeeDenormals) {
    engine::set_simulation_float_mode();
    EXPECT_TRUE(engine::simulation_float_mode_active());
    // The smallest normal float halved is a denormal, not zero, in the simulation mode.
    volatile float tiny = 1.17549435e-38f;
    volatile float half = tiny * 0.5f;
    EXPECT_NE(half, 0.f);
}

#if defined(_M_X64) || defined(__x86_64__)
TEST(SimulationFloatMode, WorldRunClearsFlushToZero) {
    const unsigned int saved = _mm_getcsr();
    _mm_setcsr(saved | 0x8040u);  // FTZ | DAZ, as an audio library might leave them
    EXPECT_FALSE(engine::simulation_float_mode_active());
    engine::ecs::World world;
    step(world, 1);
    EXPECT_TRUE(engine::simulation_float_mode_active());
    _mm_setcsr(saved);
}
#endif

// --- Deterministic trigonometry (rule 2).

TEST(DetMath, SinCosMatchesLibmWithinOneUlp) {
    for (int i = -20000; i <= 20000; ++i) {
        const float x = static_cast<float>(i) * 0.00137f;
        const engine::SinCos value = engine::det_sin_cos(x);
        const double s = std::sin(static_cast<double>(x));
        const double c = std::cos(static_cast<double>(x));
        EXPECT_NEAR(value.sin, s, ulp_of(static_cast<float>(s))) << "x=" << x;
        EXPECT_NEAR(value.cos, c, ulp_of(static_cast<float>(c))) << "x=" << x;
        EXPECT_EQ(engine::det_sin(x), value.sin);
        EXPECT_EQ(engine::det_cos(x), value.cos);
    }
}

TEST(DetMath, SinCosLargeAndSpecialArguments) {
    for (const float x : {1000.f, -12345.678f, 99999.f, 524287.f}) {
        const engine::SinCos value = engine::det_sin_cos(x);
        EXPECT_NEAR(value.sin, std::sin(static_cast<double>(x)), 1e-6) << x;
        EXPECT_NEAR(value.cos, std::cos(static_cast<double>(x)), 1e-6) << x;
    }
    const engine::SinCos huge = engine::det_sin_cos(3.0e7f);
    EXPECT_LE(std::fabs(huge.sin), 1.f);
    EXPECT_NEAR(huge.sin * huge.sin + huge.cos * huge.cos, 1.f, 1e-6f);
    EXPECT_TRUE(std::isnan(engine::det_sin_cos(INFINITY).sin));
    EXPECT_TRUE(std::isnan(engine::det_sin_cos(NAN).cos));
    EXPECT_TRUE(std::signbit(engine::det_sin_cos(-0.f).sin));
    EXPECT_EQ(engine::det_sin_cos(0.f).cos, 1.f);
}

TEST(DetMath, Atan2MatchesLibmWithinOneUlp) {
    for (int iy = -60; iy <= 60; ++iy) {
        for (int ix = -60; ix <= 60; ++ix) {
            const float y = static_cast<float>(iy) * 0.173f;
            const float x = static_cast<float>(ix) * 0.211f;
            const float expected = static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
            const float got = engine::det_atan2(y, x);
            EXPECT_NEAR(got, expected, ulp_of(expected)) << "y=" << y << " x=" << x;
        }
    }
    EXPECT_EQ(engine::det_atan2(0.f, 1.f), 0.f);
    EXPECT_TRUE(std::signbit(engine::det_atan2(-0.f, 1.f)));
    EXPECT_FLOAT_EQ(engine::det_atan2(0.f, -1.f), 3.14159265f);
    EXPECT_FLOAT_EQ(engine::det_atan2(-0.f, -1.f), -3.14159265f);
    EXPECT_FLOAT_EQ(engine::det_atan2(1.f, 0.f), 1.57079633f);
    EXPECT_FLOAT_EQ(engine::det_atan2(-INFINITY, INFINITY), -0.785398163f);
    EXPECT_TRUE(std::isnan(engine::det_atan2(NAN, 1.f)));
}

TEST(DetMath, RotationZRoundTripsThroughAngleZ) {
    for (int i = -31; i <= 31; ++i) {
        const float angle = static_cast<float>(i) * 0.1f;
        EXPECT_NEAR(engine::angle_z(engine::rotation_z(angle)), angle, 2e-6f) << angle;
    }
    const glm::quat q = engine::angle_axis(1.f, glm::vec3{0.f, 1.f, 0.f});
    EXPECT_NEAR(q.w, std::cos(0.5), 1e-7);
    EXPECT_NEAR(q.y, std::sin(0.5), 1e-7);
}
