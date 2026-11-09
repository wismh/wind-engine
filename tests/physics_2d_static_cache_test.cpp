#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include "ecs/physics_2d_internal.h"

#include <algorithm>
#include <cstdint>
#include <vector>

// LQ-22: static colliders are cached across ticks. Terrain edits must still give the right contacts the next tick.

namespace {

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

engine::ecs::World physics_world() {
    engine::ecs::World world;
    engine::register_engine_systems(world);
    return world;
}

// A kinematic box with zero velocity: active, never moves.
engine::ecs::Entity add_probe(engine::ecs::World& world, glm::vec3 position) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = {2.f, 2.f, 1.f}});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{});
    return entity;
}

// No RigidBody2D: static.
engine::ecs::Entity add_static_box(engine::ecs::World& world, glm::vec3 position, glm::vec3 size = {2.f, 2.f, 1.f}) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = size});
    return entity;
}

// Far statics created first, so the edited static sorts after them and the audit (32 per tick, from the start after a
// rebuild) cannot be what finds the edit on the next tick: only the mark can.
void add_far_padding(engine::ecs::World& world) {
    for (int i = 0; i < 256; ++i) {
        add_static_box(world, glm::vec3{3.f * static_cast<float>(i), 1000.f, 0.f});
    }
}

[[nodiscard]] bool has_event(const std::vector<engine::CollisionEvent2D>& events, engine::ecs::Entity a,
                             engine::ecs::Entity b, engine::CollisionPhase phase) {
    return std::ranges::any_of(events, [&](const engine::CollisionEvent2D& event) {
        const bool same_pair = (event.a == a && event.b == b) || (event.a == b && event.b == a);
        return same_pair && event.phase == phase;
    });
}

[[nodiscard]] std::uint64_t rebuild_count(engine::ecs::World& world) {
    return world.ctx<engine::PhysicsBroadphase2D>().rebuild_count;
}

}

TEST(Physics2DStaticCache, AddedStaticCollidesNextTick) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    add_static_box(world, glm::vec3{100.f, 0.f, 0.f});
    EXPECT_TRUE(step_and_read(world).empty());

    const engine::ecs::Entity added = add_static_box(world, glm::vec3{1.5f, 0.f, 0.f});
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, added, engine::CollisionPhase::Enter));
    EXPECT_NEAR(events[0].penetration, 0.5f, 1e-5f);
}

TEST(Physics2DStaticCache, AddedStaticPushesDynamicBodyNextTick) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity body = world.create();
    world.emplace<engine::Transform>(body, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(body, engine::BoxCollider2D{.size = {2.f, 2.f, 1.f}});
    world.emplace<engine::RigidBody2D>(body, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    step_physics(world);
    EXPECT_EQ(world.get<engine::Transform>(body).position, glm::vec3{0.f});

    add_static_box(world, glm::vec3{0.f, -1.5f, 0.f});
    step_physics(world);
    EXPECT_GT(world.get<engine::Transform>(body).position.y, 0.f);
}

TEST(Physics2DStaticCache, RemovedOrDestroyedStaticExitsNextTick) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity removed = add_static_box(world, glm::vec3{1.5f, 0.f, 0.f});
    const engine::ecs::Entity destroyed = add_static_box(world, glm::vec3{-1.5f, 0.f, 0.f});
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, removed, engine::CollisionPhase::Enter));
    EXPECT_TRUE(has_event(events, probe, destroyed, engine::CollisionPhase::Enter));

    world.remove<engine::BoxCollider2D>(removed);
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, removed, engine::CollisionPhase::Exit));
    EXPECT_TRUE(has_event(events, probe, destroyed, engine::CollisionPhase::Stay));

    world.destroy(destroyed);
    events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, destroyed, engine::CollisionPhase::Exit));
    EXPECT_TRUE(step_and_read(world).empty());
}

TEST(Physics2DStaticCache, RecycledEntityInSameTickUsesNewGeometry) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity old_static = add_static_box(world, glm::vec3{100.f, 0.f, 0.f});
    EXPECT_TRUE(step_and_read(world).empty());

    world.destroy(old_static);
    const engine::ecs::Entity recycled = add_static_box(world, glm::vec3{1.5f, 0.f, 0.f});
    ASSERT_EQ(recycled.index, old_static.index);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, recycled, engine::CollisionPhase::Enter));
}

TEST(Physics2DStaticCache, MovedStaticWithMarkCollidesNextTick) {
    engine::ecs::World world = physics_world();
    add_far_padding(world);
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity moved = add_static_box(world, glm::vec3{100.f, 0.f, 0.f});
    EXPECT_TRUE(step_and_read(world).empty());

    world.get<engine::Transform>(moved).position.x = 1.5f;
    engine::mark_static_colliders_dirty_2d(world);
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, moved, engine::CollisionPhase::Enter));

    world.get<engine::Transform>(moved).rotation = engine::rotation_z(0.5f);
    world.get<engine::Transform>(moved).position.x = 100.f;
    engine::mark_static_colliders_dirty_2d(world);
    events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, moved, engine::CollisionPhase::Exit));
}

TEST(Physics2DStaticCache, EditedStaticFieldsWithMarkApplyNextTick) {
    engine::ecs::World world = physics_world();
    add_far_padding(world);
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity box = add_static_box(world, glm::vec3{3.f, 0.f, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    const engine::ecs::Entity poly = world.create();
    world.emplace<engine::Transform>(poly, engine::Transform{.position = {0.f, 3.f, 0.f}});
    engine::PolygonCollider2D shape{};
    shape.vertices[0] = {-0.5f, -0.5f};
    shape.vertices[1] = {0.5f, -0.5f};
    shape.vertices[2] = {0.f, 0.5f};
    shape.count = 3;
    world.emplace<engine::PolygonCollider2D>(poly, shape);
    EXPECT_TRUE(step_and_read(world).empty());

    world.get<engine::BoxCollider2D>(box).size = glm::vec3{5.f, 1.f, 1.f};
    world.get<engine::PolygonCollider2D>(poly).vertices[0] = {-0.5f, -2.5f};
    engine::mark_static_colliders_dirty_2d(world);
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, box, engine::CollisionPhase::Enter));
    EXPECT_TRUE(has_event(events, probe, poly, engine::CollisionPhase::Enter));

    world.get<engine::BoxCollider2D>(box).layer = 0u;
    world.get<engine::PolygonCollider2D>(poly).is_trigger = true;
    engine::mark_static_colliders_dirty_2d(world);
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, box, engine::CollisionPhase::Exit));
    ASSERT_TRUE(has_event(events, probe, poly, engine::CollisionPhase::Stay));
    for (const engine::CollisionEvent2D& event : events) {
        if (event.a == poly || event.b == poly) {
            EXPECT_TRUE(event.is_trigger);
        }
    }
}

TEST(Physics2DStaticCache, MovedChunkParentWithMarkMovesChildren) {
    engine::ecs::World world = physics_world();
    add_far_padding(world);
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity chunk = world.create();
    world.emplace<engine::Transform>(chunk, engine::Transform{.position = {100.f, 0.f, 0.f}});
    const engine::ecs::Entity cell = add_static_box(world, glm::vec3{1.5f, 0.f, 0.f});
    ASSERT_TRUE(engine::ecs::set_parent(world, cell, chunk));
    EXPECT_TRUE(step_and_read(world).empty());

    world.get<engine::Transform>(chunk).position.x = 0.f;
    engine::mark_static_colliders_dirty_2d(world);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, cell, engine::CollisionPhase::Enter));
}

TEST(Physics2DStaticCache, UnmarkedEditIsFoundByTheAudit) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    constexpr int kStatics = 100;
    std::vector<engine::ecs::Entity> statics;
    for (int i = 0; i < kStatics; ++i) {
        statics.push_back(add_static_box(world, glm::vec3{100.f + 3.f * static_cast<float>(i), 0.f, 0.f}));
    }
    EXPECT_TRUE(step_and_read(world).empty());
    const std::uint64_t rebuilds = rebuild_count(world);

    const engine::ecs::Entity moved = statics[kStatics / 2];
    world.get<engine::Transform>(moved).position.x = 1.5f;
    // 32 cached statics are re-read per tick.
    const int bound = (kStatics + 31) / 32 + 1;
    int found_at = -1;
    for (int tick = 0; tick < bound && found_at < 0; ++tick) {
        if (has_event(step_and_read(world), probe, moved, engine::CollisionPhase::Enter)) {
            found_at = tick;
        }
    }
    EXPECT_GE(found_at, 0);
    EXPECT_EQ(rebuild_count(world), rebuilds + 1);
}

TEST(Physics2DStaticCache, IdleTicksDoNotRebuild) {
    engine::ecs::World world = physics_world();
    add_probe(world, glm::vec3{0.f});
    for (int i = 0; i < 50; ++i) {
        add_static_box(world, glm::vec3{10.f + 3.f * static_cast<float>(i), 0.f, 0.f});
    }
    step_physics(world);
    const std::uint64_t rebuilds = rebuild_count(world);
    for (int i = 0; i < 100; ++i) {
        step_physics(world);
    }
    EXPECT_EQ(rebuild_count(world), rebuilds);

    engine::mark_static_colliders_dirty_2d(world);
    step_physics(world);
    EXPECT_EQ(rebuild_count(world), rebuilds + 1);

    add_static_box(world, glm::vec3{-10.f, 0.f, 0.f});
    step_physics(world);
    EXPECT_EQ(rebuild_count(world), rebuilds + 2);
    step_physics(world);
    EXPECT_EQ(rebuild_count(world), rebuilds + 2);
}

TEST(Physics2DStaticCache, BodyTypeChangesNeedNoMark) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity ground = add_static_box(world, glm::vec3{0.f});
    const engine::ecs::Entity crate = add_static_box(world, glm::vec3{1.5f, 0.f, 0.f});
    world.emplace<engine::RigidBody2D>(crate, engine::RigidBody2D{.type = engine::BodyType::Static});
    EXPECT_TRUE(step_and_read(world).empty());

    world.get<engine::RigidBody2D>(crate).type = engine::BodyType::Kinematic;
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, ground, crate, engine::CollisionPhase::Enter));

    world.get<engine::RigidBody2D>(crate).type = engine::BodyType::Static;
    events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, ground, crate, engine::CollisionPhase::Exit));

    world.get<engine::RigidBody2D>(crate).type = engine::BodyType::Kinematic;
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, ground, crate, engine::CollisionPhase::Enter));
    world.remove<engine::RigidBody2D>(crate);
    events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, ground, crate, engine::CollisionPhase::Exit));

    // A dynamic body put on the static ground: the ground is now dynamic and is pushed out of the crate.
    world.emplace<engine::RigidBody2D>(ground, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, ground, crate, engine::CollisionPhase::Enter));
    EXPECT_LT(world.get<engine::Transform>(ground).position.x, 0.f);
}

TEST(Physics2DStaticCache, ReparentingInAndOutOfAMovingBodyNeedsNoMark) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity carrier = world.create();
    world.emplace<engine::Transform>(carrier, engine::Transform{});
    world.emplace<engine::RigidBody2D>(carrier, engine::RigidBody2D{.velocity = {60.f, 0.f, 0.f}});
    const engine::ecs::Entity cell = add_static_box(world, glm::vec3{10.f, 0.f, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{10.f, 0.f, 0.f});
    world.get<engine::BoxCollider2D>(probe).size = glm::vec3{1.f, 1.f, 1.f};
    const engine::ecs::Entity wall = add_static_box(world, glm::vec3{13.2f, 0.f, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    const engine::ecs::Entity late = add_probe(world, glm::vec3{13.f, 5.f, 0.f});
    world.get<engine::BoxCollider2D>(late).size = glm::vec3{1.f, 1.f, 1.f};

    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, cell, engine::CollisionPhase::Enter));

    ASSERT_TRUE(engine::ecs::set_parent(world, cell, carrier, engine::ecs::KeepWorld::Yes));
    (void) step_and_read(world);  // the cell is at x = 12 after the carrier's first moved tick; touching probe
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, cell, engine::CollisionPhase::Exit));
    events = step_and_read(world);
    // A stale static copy of the cell at x = 10 would still touch the probe.
    EXPECT_FALSE(has_event(events, probe, cell, engine::CollisionPhase::Stay));
    EXPECT_FALSE(has_event(events, probe, cell, engine::CollisionPhase::Enter));
    EXPECT_TRUE(has_event(events, cell, wall, engine::CollisionPhase::Enter));

    // Static again at its last pose. Static-static is not a pair, and the cache has the new pose.
    engine::ecs::clear_parent(world, cell, engine::ecs::KeepWorld::Yes);
    const float cell_x = world.get<engine::Transform>(cell).position.x;
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, cell, wall, engine::CollisionPhase::Exit));
    EXPECT_EQ(world.get<engine::Transform>(cell).position.x, cell_x);

    world.get<engine::Transform>(late).position = glm::vec3{cell_x, 0.f, 0.f};
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, late, cell, engine::CollisionPhase::Enter));
}

TEST(Physics2DStaticCache, StaticEntityWithTwoShapesPublishesOnePair) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity rock = add_static_box(world, glm::vec3{1.5f, 0.f, 0.f});
    world.emplace<engine::CircleCollider2D>(rock, engine::CircleCollider2D{.radius = 1.f});
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, rock, engine::CollisionPhase::Enter));
    // Both shape pairs are 0.5 deep. Equal depth keeps the earlier pair: the box, gathered before the circle.
    EXPECT_NEAR(events[0].penetration, 0.5f, 1e-5f);
}
