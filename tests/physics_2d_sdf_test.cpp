#include <gtest/gtest.h>

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include "ecs/contact_2d.h"
#include "ecs/physics_2d_internal.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

// LQ-26: SdfCollider2D, SDF terrain in the 2D solver.

namespace {

constexpr float kCell = 0.25f;
constexpr float kBand = 1.0f;
// Chunk width in world units: 32 cells.
constexpr float kChunk = kCell * static_cast<float>(engine::kSdfChunkCells);
// The ground's top surface.
constexpr float kGround = 0.5f;

void step_physics(engine::ecs::World& world) {
    world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    world.run(engine::ecs::Schedule::Fixed);
}

std::vector<engine::CollisionEvent2D> step_and_read(engine::ecs::World& world) {
    world.flush_events();
    world.flush_events();
    step_physics(world);
    std::vector<engine::CollisionEvent2D> events;
    for (const engine::CollisionEvent2D& event : engine::ecs::EventReader<engine::CollisionEvent2D>{world}) {
        events.push_back(event);
    }
    return events;
}

engine::ecs::World physics_world(engine::JobSystem* jobs = nullptr, glm::vec3 gravity = {0.f, -10.f, 0.f}) {
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.jobs = jobs});
    world.ctx<engine::Physics2DConfig>().gravity = gravity;
    return world;
}

// Solid below y = kGround over x in [x_min, x_max], three units deep.
engine::SdfField2D ground_field(float x_min = -6.f, float x_max = 6.f) {
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    const std::array<glm::vec2, 4> ground{
            glm::vec2{x_min, -3.f}, glm::vec2{x_max, -3.f}, glm::vec2{x_max, kGround}, glm::vec2{x_min, kGround}};
    (void)field.fill_polygon(ground);
    return field;
}

engine::ecs::Entity add_terrain(engine::ecs::World& world, engine::SdfField2D field,
                                engine::Transform transform = engine::Transform{}) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, transform);
    world.emplace<engine::SdfCollider2D>(entity, engine::SdfCollider2D{.field = std::move(field)});
    return entity;
}

engine::ecs::Entity add_dynamic_circle(engine::ecs::World& world, glm::vec2 position, float radius = 0.4f) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = {position, 0.f}});
    world.emplace<engine::CircleCollider2D>(entity, engine::CircleCollider2D{.radius = radius});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    return entity;
}

engine::ecs::Entity add_dynamic_box(engine::ecs::World& world, glm::vec2 position, float friction = 0.5f) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = {position, 0.f}});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = {1.f, 1.f, 1.f}, .friction = friction});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    return entity;
}

[[nodiscard]] glm::vec2 position_of(const engine::ecs::World& world, engine::ecs::Entity entity) {
    const glm::vec3 p = world.get<engine::Transform>(entity).position;
    return {p.x, p.y};
}

void run_ticks(engine::ecs::World& world, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        step_physics(world);
    }
}

[[nodiscard]] int count_phase(const std::vector<engine::CollisionEvent2D>& events, engine::ecs::Entity a,
                              engine::ecs::Entity b, engine::CollisionPhase phase) {
    return static_cast<int>(std::ranges::count_if(events, [&](const engine::CollisionEvent2D& event) {
        const bool same_pair = (event.a == a && event.b == b) || (event.a == b && event.b == a);
        return same_pair && event.phase == phase;
    }));
}

[[nodiscard]] bool contains(const std::vector<engine::ecs::Entity>& sorted, engine::ecs::Entity entity) {
    return std::binary_search(sorted.begin(), sorted.end(), entity);
}

[[nodiscard]] engine::PhysicsBroadphase2D& cache_of(engine::ecs::World& world) {
    return world.ctx<engine::PhysicsBroadphase2D>();
}

}

TEST(Physics2DSdf, PartEncodesChunkInCoordinateOrder) {
    const glm::ivec2 chunks[] = {{-32768, -32768}, {5, -1}, {-3, 0}, {0, 0}, {7, 0}, {-1, 2}, {32767, 32767}};
    for (std::size_t i = 0; i < std::size(chunks); ++i) {
        EXPECT_TRUE(engine::sdf_chunk_has_part_2d(chunks[i]));
        EXPECT_EQ(engine::sdf_chunk_of_part_2d(engine::sdf_part_of_chunk_2d(chunks[i])), chunks[i]);
        if (i > 0) {
            EXPECT_LT(engine::sdf_part_of_chunk_2d(chunks[i - 1]), engine::sdf_part_of_chunk_2d(chunks[i]));
        }
    }
    EXPECT_FALSE(engine::sdf_chunk_has_part_2d({32768, 0}));
    EXPECT_FALSE(engine::sdf_chunk_has_part_2d({0, -32769}));
}

TEST(Physics2DSdf, CircleManifoldPointsIntoTheSolid) {
    const engine::SdfField2D field = ground_field();
    engine::ContactShape2D terrain{.kind = engine::ContactShapeKind::Sdf, .sdf = &field, .sdf_chunk = {0, 0}};
    engine::ContactShape2D circle{.kind = engine::ContactShapeKind::Circle, .center = {2.f, 0.8f}, .radius = 0.4f};

    const engine::ContactManifold2D down = engine::make_contact_2d(circle, terrain);
    ASSERT_EQ(down.count, 1);
    EXPECT_NEAR(down.normal.x, 0.f, 1e-3f);
    EXPECT_NEAR(down.normal.y, -1.f, 1e-3f);
    EXPECT_NEAR(down.points[0].penetration, 0.1f, 2e-3f);
    EXPECT_NEAR(down.points[0].point.y, 0.45f, 2e-3f);

    // Reversed order: from the field toward the circle.
    const engine::ContactManifold2D up = engine::make_contact_2d(terrain, circle);
    ASSERT_EQ(up.count, 1);
    EXPECT_NEAR(up.normal.y, 1.f, 1e-3f);

    // Separated, and a sample the chunk does not own.
    circle.center = {2.f, 1.f};
    EXPECT_EQ(engine::make_contact_2d(circle, terrain).count, 0);
    circle.center = {-2.f, 0.8f};
    EXPECT_EQ(engine::make_contact_2d(circle, terrain).count, 0);
    terrain.sdf_chunk = {-1, 0};
    EXPECT_EQ(engine::make_contact_2d(circle, terrain).count, 1);

    // Field against field.
    EXPECT_EQ(engine::make_contact_2d(terrain, terrain).count, 0);
}

TEST(Physics2DSdf, BoxManifoldSpansTheContact) {
    const engine::SdfField2D field = ground_field();
    const engine::ContactShape2D terrain{.kind = engine::ContactShapeKind::Sdf, .sdf = &field, .sdf_chunk = {0, 0}};
    const engine::ContactShape2D box{
            .kind = engine::ContactShapeKind::Aabb, .center = {2.f, 0.9f}, .half_extents = {0.5f, 0.5f}};
    const engine::ContactManifold2D manifold = engine::make_contact_2d(box, terrain);
    ASSERT_EQ(manifold.count, 2);
    EXPECT_NEAR(manifold.normal.y, -1.f, 1e-3f);
    for (int i = 0; i < 2; ++i) {
        EXPECT_NEAR(manifold.points[i].penetration, 0.1f, 2e-3f);
    }
    EXPECT_NEAR(std::abs(manifold.points[0].point.x - manifold.points[1].point.x), 1.f, 1e-4f);
}

TEST(Physics2DSdf, CircleRestsAndSleeps) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity terrain = add_terrain(world, ground_field());
    const engine::ecs::Entity ball = add_dynamic_circle(world, {2.f, 2.f});
    run_ticks(world, 239);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);

    EXPECT_NEAR(position_of(world, ball).y, kGround + 0.4f, 0.03f);
    EXPECT_NEAR(position_of(world, ball).x, 2.f, 1e-3f);
    EXPECT_TRUE(world.get<engine::RigidBody2D>(ball).sleeping);
    ASSERT_EQ(count_phase(events, terrain, ball, engine::CollisionPhase::Stay), 1);
    const engine::CollisionEvent2D& event = events.front();
    EXPECT_EQ(event.a, terrain);
    EXPECT_NEAR(event.normal.y, 1.f, 1e-3f);
    EXPECT_TRUE(contains(cache_of(world).static_entities, terrain));
}

TEST(Physics2DSdf, BoxRestsFlatAndSleeps) {
    engine::ecs::World world = physics_world();
    (void)add_terrain(world, ground_field());
    const engine::ecs::Entity box = add_dynamic_box(world, {2.5f, 3.f});
    run_ticks(world, 300);

    EXPECT_NEAR(position_of(world, box).y, kGround + 0.5f, 0.03f);
    EXPECT_NEAR(position_of(world, box).x, 2.5f, 0.02f);
    EXPECT_NEAR(engine::angle_z(world.get<engine::Transform>(box).rotation), 0.f, 1e-3f);
    EXPECT_TRUE(world.get<engine::RigidBody2D>(box).sleeping);
}

TEST(Physics2DSdf, SlidingAcrossAChunkSeamStaysOneStream) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity terrain = add_terrain(world, ground_field(-12.f, 12.f));
    ASSERT_GE(world.get<engine::SdfCollider2D>(terrain).field.chunk_count(), 4u);
    const engine::ecs::Entity box = add_dynamic_box(world, {-3.f, 1.2f}, 0.f);
    run_ticks(world, 60);

    int stays = 0;
    for (int tick = 0; tick < 150; ++tick) {
        world.get<engine::RigidBody2D>(box).velocity.x = 3.f;
        const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
        EXPECT_EQ(count_phase(events, terrain, box, engine::CollisionPhase::Exit), 0) << "tick " << tick;
        EXPECT_EQ(count_phase(events, terrain, box, engine::CollisionPhase::Enter), 0) << "tick " << tick;
        stays += count_phase(events, terrain, box, engine::CollisionPhase::Stay);
    }
    EXPECT_EQ(stays, 150);
    EXPECT_GT(position_of(world, box).x, 3.f);
    EXPECT_NEAR(position_of(world, box).y, kGround + 0.5f, 0.05f);
}

TEST(Physics2DSdf, RotatedTerrainIsAWall) {
    // Turned 90 degrees: local "up" is world -X, so the solid fills x > -kGround.
    engine::ecs::World world = physics_world(nullptr, {10.f, 0.f, 0.f});
    (void)add_terrain(world, ground_field(),
                      engine::Transform{.rotation = engine::rotation_z(glm::half_pi<float>())});
    const engine::ecs::Entity ball = add_dynamic_circle(world, {-2.f, 1.f});
    run_ticks(world, 240);
    EXPECT_NEAR(position_of(world, ball).x, -kGround - 0.4f, 0.03f);
    EXPECT_NEAR(position_of(world, ball).y, 1.f, 1e-3f);
}

TEST(Physics2DSdf, KinematicTerrainPushesADynamic) {
    engine::ecs::World world = physics_world(nullptr, glm::vec3{0.f});
    engine::SdfField2D block{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    const std::array<glm::vec2, 4> square{glm::vec2{-0.5f, -0.5f}, glm::vec2{0.5f, -0.5f}, glm::vec2{0.5f, 0.5f},
                                          glm::vec2{-0.5f, 0.5f}};
    (void)block.fill_polygon(square);
    const engine::ecs::Entity pusher = add_terrain(world, std::move(block));
    world.emplace<engine::RigidBody2D>(pusher, engine::RigidBody2D{.velocity = {2.f, 0.f, 0.f}});
    const engine::ecs::Entity ball = add_dynamic_circle(world, {1.5f, 0.f});
    run_ticks(world, 90);

    const float pusher_x = world.get<engine::Transform>(pusher).position.x;
    EXPECT_NEAR(pusher_x, 3.f, 1e-3f);
    EXPECT_GT(position_of(world, ball).x, pusher_x + 0.5f);
    EXPECT_GT(world.get<engine::RigidBody2D>(ball).velocity.x, 1.f);
    EXPECT_FALSE(contains(cache_of(world).static_entities, pusher));
}

TEST(Physics2DSdf, CarveUnderASleepingBoxWakesItWhenMarked) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity terrain = add_terrain(world, ground_field(-16.f, 6.f));
    const engine::ecs::Entity box = add_dynamic_box(world, {2.5f, 1.2f});
    const engine::ecs::Entity far = add_dynamic_box(world, {-12.f, 1.2f});
    run_ticks(world, 300);
    ASSERT_TRUE(world.get<engine::RigidBody2D>(box).sleeping);
    ASSERT_TRUE(world.get<engine::RigidBody2D>(far).sleeping);
    const float rest_y = position_of(world, box).y;

    const std::vector<glm::ivec2> dirty =
            world.get<engine::SdfCollider2D>(terrain).field.carve_circle({2.5f, kGround}, 1.5f);
    ASSERT_FALSE(dirty.empty());
    engine::mark_static_colliders_dirty_2d(world, terrain);
    step_physics(world);
    EXPECT_FALSE(world.get<engine::RigidBody2D>(box).sleeping);
    EXPECT_TRUE(world.get<engine::RigidBody2D>(far).sleeping);

    run_ticks(world, 60);
    EXPECT_LT(position_of(world, box).y, rest_y - 0.6f);
    EXPECT_TRUE(cache_of(world).audit_warned.empty());
}

TEST(Physics2DSdf, CarveUnderHalfASleepingBoxWakesIt) {
    // The box keeps touching the ground on its left, so only the edit wake can wake it: a sleeper in contact with
    // nothing moving stays asleep.
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity terrain = add_terrain(world, ground_field());
    const engine::ecs::Entity box = add_dynamic_box(world, {2.5f, 1.2f});
    run_ticks(world, 300);
    ASSERT_TRUE(world.get<engine::RigidBody2D>(box).sleeping);

    (void)world.get<engine::SdfCollider2D>(terrain).field.carve_circle({3.4f, kGround}, 1.1f);
    engine::mark_static_colliders_dirty_2d(world, terrain);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_EQ(count_phase(events, terrain, box, engine::CollisionPhase::Stay), 1);
    EXPECT_FALSE(world.get<engine::RigidBody2D>(box).sleeping);
}

TEST(Physics2DSdf, ForgottenMarkIsCaughtByTheAudit) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity terrain = add_terrain(world, ground_field());
    const engine::ecs::Entity box = add_dynamic_box(world, {2.5f, 1.2f});
    run_ticks(world, 300);
    ASSERT_TRUE(world.get<engine::RigidBody2D>(box).sleeping);
    const std::uint64_t rebuilds = cache_of(world).rebuild_count;

    // Fill a new chunk far away: no existing slot changed, but the field's edit count did.
    const std::array<glm::vec2, 4> island{glm::vec2{40.f, 40.f}, glm::vec2{41.f, 40.f}, glm::vec2{41.f, 41.f},
                                          glm::vec2{40.f, 41.f}};
    (void)world.get<engine::SdfCollider2D>(terrain).field.fill_polygon(island);
    const std::size_t slots_before = cache_of(world).static_keys.size();
    run_ticks(world, 4);
    EXPECT_TRUE(contains(cache_of(world).audit_warned, terrain));
    EXPECT_GT(cache_of(world).rebuild_count, rebuilds);
    EXPECT_GT(cache_of(world).static_keys.size(), slots_before);
}

TEST(Physics2DSdf, MarkRereadsOnlyTheChangedChunks) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity terrain = add_terrain(world, ground_field(-20.f, 20.f));
    const std::size_t chunks = world.get<engine::SdfCollider2D>(terrain).field.chunk_count();
    ASSERT_GE(chunks, 8u);
    run_ticks(world, 2);
    EXPECT_EQ(cache_of(world).static_keys.size(), chunks);
    const std::uint64_t rebuilds = cache_of(world).rebuild_count;
    const std::uint64_t entity_rebuilds = cache_of(world).entity_rebuild_count;

    // In the middle of chunk (0, -1): no neighbour reads a changed sample.
    const std::vector<glm::ivec2> dirty =
            world.get<engine::SdfCollider2D>(terrain).field.carve_circle({kChunk * 0.5f, -1.5f}, 0.3f);
    ASSERT_EQ(dirty.size(), 1u);
    engine::mark_static_colliders_dirty_2d(world, terrain);
    step_physics(world);
    EXPECT_EQ(cache_of(world).rebuild_count, rebuilds);
    EXPECT_EQ(cache_of(world).entity_rebuild_count, entity_rebuilds + 1);
    EXPECT_EQ(cache_of(world).last_rebuild_reads, chunks);
    EXPECT_EQ(cache_of(world).last_rebuild_changes, 1u);

    // A mark with no edit changes nothing.
    engine::mark_static_colliders_dirty_2d(world, terrain);
    step_physics(world);
    EXPECT_EQ(cache_of(world).last_rebuild_changes, 0u);
    run_ticks(world, 8);
    EXPECT_TRUE(cache_of(world).audit_warned.empty());
}

TEST(Physics2DSdf, UnderADynamicBodyIsIgnoredWithAWarning) {
    engine::ecs::World world = physics_world(nullptr, glm::vec3{0.f});
    const engine::ecs::Entity ignored = add_terrain(world, ground_field());
    world.emplace<engine::RigidBody2D>(ignored, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    const engine::ecs::Entity probe = world.create();
    world.emplace<engine::Transform>(probe, engine::Transform{.position = {2.f, 0.f, 0.f}});
    world.emplace<engine::BoxCollider2D>(probe, engine::BoxCollider2D{});
    world.emplace<engine::RigidBody2D>(probe, engine::RigidBody2D{});

    for (int tick = 0; tick < 3; ++tick) {
        const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
        EXPECT_TRUE(events.empty());
    }
    EXPECT_TRUE(contains(cache_of(world).dynamic_terrain_warned, ignored));
    EXPECT_EQ(cache_of(world).dynamic_terrain_warned.size(), 1u);
    EXPECT_FALSE(contains(cache_of(world).static_entities, ignored));
}

TEST(Physics2DSdf, LayerAndMaskFilter) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity terrain = add_terrain(world, ground_field());
    world.get<engine::SdfCollider2D>(terrain).layer = 2u;
    const engine::ecs::Entity rests = add_dynamic_circle(world, {2.f, 2.f});
    const engine::ecs::Entity falls = add_dynamic_circle(world, {-2.f, 2.f});
    world.get<engine::CircleCollider2D>(falls).mask = ~2u;
    run_ticks(world, 120);
    EXPECT_NEAR(position_of(world, rests).y, kGround + 0.4f, 0.03f);
    EXPECT_LT(position_of(world, falls).y, -3.f);
}

TEST(Physics2DSdf, BodiesInAnUnallocatedSolidRegionGetNoContact) {
    engine::ecs::World world = physics_world(nullptr, glm::vec3{0.f});
    engine::SdfField2D solid{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand,
                                                     .outside = engine::SdfOutside::Solid}};
    (void)solid.carve_circle({2.f, 2.f}, 1.f);
    const engine::ecs::Entity terrain = add_terrain(world, std::move(solid));
    const engine::ecs::Entity inside_hole = add_dynamic_circle(world, {2.f, 2.f}, 0.4f);
    const engine::ecs::Entity buried = add_dynamic_circle(world, {-30.f, -30.f}, 0.4f);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_EQ(count_phase(events, terrain, inside_hole, engine::CollisionPhase::Enter), 0);
    EXPECT_EQ(count_phase(events, terrain, buried, engine::CollisionPhase::Enter), 0);

    // Against the hole's wall it does touch.
    world.get<engine::Transform>(inside_hole).position = {2.f, 2.7f, 0.f};
    const std::vector<engine::CollisionEvent2D> wall = step_and_read(world);
    EXPECT_EQ(count_phase(wall, terrain, inside_hole, engine::CollisionPhase::Enter), 1);
}

namespace {

[[nodiscard]] std::uint64_t hash_bodies(engine::ecs::World& world) {
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&](float value) {
        hash ^= std::bit_cast<std::uint32_t>(value);
        hash *= 1099511628211ull;
    };
    world.view<engine::Transform, engine::RigidBody2D>().each(
            [&](engine::ecs::Entity, const engine::Transform& transform, const engine::RigidBody2D& body) {
                mix(transform.position.x);
                mix(transform.position.y);
                mix(transform.rotation.z);
                mix(transform.rotation.w);
                mix(body.velocity.x);
                mix(body.velocity.y);
                mix(body.angular_velocity);
            });
    return hash;
}

[[nodiscard]] std::vector<std::uint64_t> run_pile(int workers) {
    engine::JobSystem jobs(workers);
    engine::ecs::World world = physics_world(&jobs);
    engine::SdfField2D field = ground_field(-20.f, 20.f);
    (void)field.carve_circle({0.f, kGround}, 2.f);
    (void)add_terrain(world, std::move(field));
    for (int i = 0; i < 48; ++i) {
        const float x = -12.f + static_cast<float>(i % 16) * 1.5f;
        const float y = 2.f + static_cast<float>(i / 16) * 1.3f;
        if (i % 3 == 0) {
            (void)add_dynamic_circle(world, {x + 0.1f, y});
        } else {
            (void)add_dynamic_box(world, {x, y});
        }
    }
    std::vector<std::uint64_t> hashes;
    for (int step = 0; step < 120; ++step) {
        step_physics(world);
        hashes.push_back(hash_bodies(world));
    }
    return hashes;
}

}

TEST(Physics2DSdf, DeterministicAcrossWorkerCounts) {
    const std::vector<std::uint64_t> serial = run_pile(0);
    EXPECT_NE(serial.front(), serial.back());
    for (const int workers : {2, 4}) {
        EXPECT_EQ(run_pile(workers), serial) << "workers " << workers;
    }
}
