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
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

// LQ-28: mark_static_colliders_dirty_2d(world, entity) re-reads only that entity's subtree, and the tick it gives is
// the tick a world-wide re-read gives.

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

engine::PhysicsBroadphase2D& cache_of(engine::ecs::World& world) {
    return world.ctx<engine::PhysicsBroadphase2D>();
}

// A kinematic box with zero velocity: active, never moves.
engine::ecs::Entity add_probe(engine::ecs::World& world, glm::vec3 position) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = {2.f, 2.f, 1.f}});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{});
    return entity;
}

engine::ecs::Entity add_static_box(engine::ecs::World& world, glm::vec3 position, glm::vec3 size = {2.f, 2.f, 1.f}) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = size});
    return entity;
}

// `count` unit boxes in a row from (x0, y) in the chunk's local XY.
std::vector<engine::CompoundShape2D> box_row(int count, float x0, float y) {
    std::vector<engine::CompoundShape2D> shapes;
    for (int i = 0; i < count; ++i) {
        shapes.push_back(engine::box_shape_2d({x0 + static_cast<float>(i), y}, {1.f, 1.f}));
    }
    return shapes;
}

engine::ecs::Entity add_compound(engine::ecs::World& world, glm::vec3 position,
                                 std::vector<engine::CompoundShape2D> shapes) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::CompoundCollider2D>(entity, engine::CompoundCollider2D{.shapes = std::move(shapes)});
    return entity;
}

// Far statics created first, so the edited static sorts after them and the audit (32 per tick from slot 0) cannot be
// what finds the edit on the next tick: only the mark can.
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

[[nodiscard]] bool contains_static(engine::ecs::World& world, engine::ecs::Entity entity) {
    const std::vector<engine::ecs::Entity>& statics = cache_of(world).static_entities;
    return std::binary_search(statics.begin(), statics.end(), entity);
}

struct Counts {
    std::uint64_t full{0};
    std::uint64_t entity{0};
};

[[nodiscard]] Counts counts(engine::ecs::World& world) {
    const engine::PhysicsBroadphase2D& cache = cache_of(world);
    return Counts{cache.rebuild_count, cache.entity_rebuild_count};
}

}

TEST(Physics2DStaticDirty, MarkingOneChunkReadsOnlyItsParts) {
    engine::ecs::World world = physics_world();
    constexpr int kChunks = 10;
    constexpr int kParts = 50;
    std::vector<engine::ecs::Entity> chunks;
    for (int c = 0; c < kChunks; ++c) {
        chunks.push_back(add_compound(world, glm::vec3{100.f * static_cast<float>(c + 1), 0.f, 0.f},
                                      box_row(kParts, 0.f, 0.f)));
    }
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    EXPECT_TRUE(step_and_read(world).empty());
    EXPECT_EQ(cache_of(world).last_rebuild_reads, static_cast<std::size_t>(kChunks * kParts));
    const Counts before = counts(world);

    const engine::ecs::Entity edited = chunks[kChunks - 1];
    world.get<engine::CompoundCollider2D>(edited).shapes[kParts / 2] =
            engine::box_shape_2d({-100.f * kChunks + 1.f, 0.f}, {1.f, 1.f});
    engine::mark_static_colliders_dirty_2d(world, edited);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, edited, engine::CollisionPhase::Enter));
    EXPECT_EQ(counts(world).full, before.full);
    EXPECT_EQ(counts(world).entity, before.entity + 1);
    EXPECT_EQ(cache_of(world).last_rebuild_reads, static_cast<std::size_t>(kParts));
    EXPECT_EQ(cache_of(world).static_keys.size(), static_cast<std::size_t>(kChunks * kParts));

    // Idle ticks after the mark re-read nothing.
    (void) step_and_read(world);
    EXPECT_EQ(counts(world).entity, before.entity + 1);
}

TEST(Physics2DStaticDirty, MovedBoxAfterEntityMarkCollidesAtItsNewPosition) {
    engine::ecs::World world = physics_world();
    add_far_padding(world);
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity moved = add_static_box(world, glm::vec3{100.f, 0.f, 0.f});
    EXPECT_TRUE(step_and_read(world).empty());
    const Counts before = counts(world);

    world.get<engine::Transform>(moved).position.x = 1.5f;
    engine::mark_static_colliders_dirty_2d(world, moved);
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, moved, engine::CollisionPhase::Enter));
    EXPECT_NEAR(events[0].penetration, 0.5f, 1e-5f);
    EXPECT_EQ(counts(world).full, before.full);
    EXPECT_EQ(cache_of(world).last_rebuild_reads, 1u);

    world.get<engine::Transform>(moved).position.x = -100.f;
    engine::mark_static_colliders_dirty_2d(world, moved);
    events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, moved, engine::CollisionPhase::Exit));
    EXPECT_EQ(counts(world).full, before.full);
}

TEST(Physics2DStaticDirty, MarkOnTheParentUpdatesChildStatics) {
    engine::ecs::World world = physics_world();
    add_far_padding(world);
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity chunk = world.create();
    world.emplace<engine::Transform>(chunk, engine::Transform{.position = {100.f, 0.f, 0.f}});
    std::vector<engine::ecs::Entity> cells;
    for (int i = 0; i < 3; ++i) {
        cells.push_back(add_static_box(world, glm::vec3{1.5f, 3.f * static_cast<float>(i), 0.f}));
        ASSERT_TRUE(engine::ecs::set_parent(world, cells.back(), chunk));
    }
    // A grandchild: an ancestor's Transform moves the whole subtree.
    const engine::ecs::Entity grandchild = add_static_box(world, glm::vec3{-3.f, 0.f, 0.f});
    ASSERT_TRUE(engine::ecs::set_parent(world, grandchild, cells[0]));
    // A kinematic child is movable: gathered every tick, never re-read by the mark.
    const engine::ecs::Entity rider = add_static_box(world, glm::vec3{0.f, -50.f, 0.f});
    world.emplace<engine::RigidBody2D>(rider, engine::RigidBody2D{});
    ASSERT_TRUE(engine::ecs::set_parent(world, rider, chunk));
    EXPECT_TRUE(step_and_read(world).empty());
    const Counts before = counts(world);

    world.get<engine::Transform>(chunk).position.x = 0.f;
    engine::mark_static_colliders_dirty_2d(world, chunk);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, cells[0], engine::CollisionPhase::Enter));
    EXPECT_TRUE(has_event(events, probe, grandchild, engine::CollisionPhase::Enter));
    EXPECT_FALSE(has_event(events, probe, cells[1], engine::CollisionPhase::Enter));
    EXPECT_EQ(counts(world).full, before.full);
    EXPECT_EQ(counts(world).entity, before.entity + 1);
    EXPECT_EQ(cache_of(world).last_rebuild_reads, 4u);  // three cells and the grandchild; the chunk has no collider
}

TEST(Physics2DStaticDirty, CompoundThatGrowsOrShrinksChangesItsSlotCount) {
    engine::ecs::World world = physics_world();
    add_far_padding(world);
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    // Parts at x = 10, 11: clear of the probe (half extent 1).
    const engine::ecs::Entity chunk = add_compound(world, glm::vec3{0.f}, box_row(2, 10.f, 0.f));
    EXPECT_TRUE(step_and_read(world).empty());
    const Counts before = counts(world);
    const std::size_t padding = 256;
    EXPECT_EQ(cache_of(world).static_keys.size(), padding + 2);

    // Grow to 5 parts; the last one reaches the probe.
    std::vector<engine::CompoundShape2D>& shapes = world.get<engine::CompoundCollider2D>(chunk).shapes;
    shapes.push_back(engine::box_shape_2d({20.f, 0.f}, {1.f, 1.f}));
    shapes.push_back(engine::box_shape_2d({30.f, 0.f}, {1.f, 1.f}));
    shapes.push_back(engine::box_shape_2d({1.f, 0.f}, {1.f, 1.f}));
    engine::mark_static_colliders_dirty_2d(world, chunk);
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, chunk, engine::CollisionPhase::Enter));
    EXPECT_EQ(cache_of(world).static_keys.size(), padding + 5);
    EXPECT_EQ(cache_of(world).last_rebuild_reads, 5u);

    // Shrink to 1 part, away from the probe.
    world.get<engine::CompoundCollider2D>(chunk).shapes.resize(1);
    engine::mark_static_colliders_dirty_2d(world, chunk);
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, chunk, engine::CollisionPhase::Exit));
    EXPECT_EQ(cache_of(world).static_keys.size(), padding + 1);

    // No part left: the chunk is no longer cached as static. Then one part again, on the probe.
    world.get<engine::CompoundCollider2D>(chunk).shapes.clear();
    engine::mark_static_colliders_dirty_2d(world, chunk);
    EXPECT_TRUE(step_and_read(world).empty());
    EXPECT_EQ(cache_of(world).static_keys.size(), padding);
    EXPECT_FALSE(contains_static(world, chunk));

    world.get<engine::CompoundCollider2D>(chunk).shapes.push_back(engine::box_shape_2d({0.5f, 0.f}, {1.f, 1.f}));
    engine::mark_static_colliders_dirty_2d(world, chunk);
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, chunk, engine::CollisionPhase::Enter));
    EXPECT_TRUE(contains_static(world, chunk));
    EXPECT_EQ(counts(world).full, before.full);
    EXPECT_EQ(counts(world).entity, before.entity + 4);
}

TEST(Physics2DStaticDirty, SeveralMarksInOneTickAreOneRebuild) {
    engine::ecs::World world = physics_world();
    add_far_padding(world);
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity a = add_static_box(world, glm::vec3{100.f, 0.f, 0.f});
    const engine::ecs::Entity b = add_static_box(world, glm::vec3{-100.f, 0.f, 0.f});
    EXPECT_TRUE(step_and_read(world).empty());
    const Counts before = counts(world);

    world.get<engine::Transform>(a).position.x = 1.5f;
    world.get<engine::Transform>(b).position.x = -1.5f;
    engine::mark_static_colliders_dirty_2d(world, a);
    engine::mark_static_colliders_dirty_2d(world, b);
    engine::mark_static_colliders_dirty_2d(world, a);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_EQ(events.size(), 2u);
    EXPECT_TRUE(has_event(events, probe, a, engine::CollisionPhase::Enter));
    EXPECT_TRUE(has_event(events, probe, b, engine::CollisionPhase::Enter));
    EXPECT_EQ(counts(world).entity, before.entity + 1);
    EXPECT_EQ(cache_of(world).last_rebuild_reads, 2u);
}

TEST(Physics2DStaticDirty, WorldMarkInTheSameTickWins) {
    engine::ecs::World world = physics_world();
    add_far_padding(world);
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity marked = add_static_box(world, glm::vec3{100.f, 0.f, 0.f});
    const engine::ecs::Entity unmarked = add_static_box(world, glm::vec3{-100.f, 0.f, 0.f});
    EXPECT_TRUE(step_and_read(world).empty());
    const Counts before = counts(world);

    world.get<engine::Transform>(marked).position.x = 1.5f;
    world.get<engine::Transform>(unmarked).position.x = -1.5f;
    engine::mark_static_colliders_dirty_2d(world, marked);
    engine::mark_static_colliders_dirty_2d(world);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, marked, engine::CollisionPhase::Enter));
    EXPECT_TRUE(has_event(events, probe, unmarked, engine::CollisionPhase::Enter));
    EXPECT_EQ(counts(world).full, before.full + 1);
    EXPECT_EQ(counts(world).entity, before.entity);
    EXPECT_TRUE(cache_of(world).marked_entities.empty());

    // The pending marks went with the full re-read: the next tick re-reads nothing.
    (void) step_and_read(world);
    EXPECT_EQ(counts(world).full, before.full + 1);
    EXPECT_EQ(counts(world).entity, before.entity);
}

TEST(Physics2DStaticDirty, DeadOrMovableEntityIsANoOp) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity doomed = add_static_box(world, glm::vec3{1.5f, 0.f, 0.f});
    EXPECT_TRUE(has_event(step_and_read(world), probe, doomed, engine::CollisionPhase::Enter));
    Counts before = counts(world);

    world.destroy(doomed);
    engine::mark_static_colliders_dirty_2d(world, doomed);
    EXPECT_TRUE(cache_of(world).marked_entities.empty());
    // The destroy is noticed on its own (structure version): one full re-read.
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, doomed, engine::CollisionPhase::Exit));
    EXPECT_EQ(counts(world).full, before.full + 1);
    EXPECT_EQ(counts(world).entity, before.entity);

    // Marked, then destroyed before the tick: the full re-read covers it.
    const engine::ecs::Entity late = add_static_box(world, glm::vec3{50.f, 0.f, 0.f});
    (void) step_and_read(world);
    before = counts(world);
    engine::mark_static_colliders_dirty_2d(world, late);
    world.destroy(late);
    (void) step_and_read(world);
    EXPECT_EQ(counts(world).full, before.full + 1);
    EXPECT_EQ(counts(world).entity, before.entity);

    // A movable entity and an entity with no collider: nothing to re-read, nothing changes.
    const std::size_t keys = cache_of(world).static_keys.size();
    engine::mark_static_colliders_dirty_2d(world, probe);
    const engine::ecs::Entity bare = world.create();
    engine::mark_static_colliders_dirty_2d(world, bare);
    before = counts(world);
    (void) step_and_read(world);
    EXPECT_EQ(counts(world).full, before.full);
    EXPECT_EQ(cache_of(world).last_rebuild_reads, 0u);
    EXPECT_EQ(cache_of(world).static_keys.size(), keys);
}

namespace {

// The same scene in two worlds; one marks per entity, the other marks the world. Dynamic boxes rest on the terrain so
// the solver and its contact cache take part.
struct DeterminismScene {
    engine::ecs::World world = physics_world();
    std::vector<engine::ecs::Entity> cells;
    engine::ecs::Entity parent{};
    std::vector<engine::ecs::Entity> children;
    engine::ecs::Entity chunk{};
    engine::ecs::Entity sensor{};
    std::vector<engine::ecs::Entity> bodies;

    DeterminismScene() {
        world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -20.f, 0.f};
        for (int i = 0; i < 12; ++i) {
            cells.push_back(add_static_box(world, glm::vec3{static_cast<float>(i) + 0.5f, -0.5f, 0.f},
                                           glm::vec3{1.f, 1.f, 1.f}));
            world.get<engine::BoxCollider2D>(cells.back()).friction = 0.4f;
        }
        parent = world.create();
        world.emplace<engine::Transform>(parent, engine::Transform{.position = {14.f, 0.f, 0.f}});
        for (int i = 0; i < 4; ++i) {
            children.push_back(add_static_box(world, glm::vec3{static_cast<float>(i) + 0.5f, -0.5f, 0.f},
                                              glm::vec3{1.f, 1.f, 1.f}));
            (void) engine::ecs::set_parent(world, children.back(), parent);
        }
        chunk = add_compound(world, glm::vec3{20.f, 0.f, 0.f}, box_row(8, 0.5f, -0.5f));
        world.get<engine::CompoundCollider2D>(chunk).friction = 0.4f;
        sensor = add_static_box(world, glm::vec3{4.f, 3.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
        world.get<engine::BoxCollider2D>(sensor).is_trigger = true;
        for (int i = 0; i < 8; ++i) {
            const engine::ecs::Entity body = world.create();
            world.emplace<engine::Transform>(
                    body, engine::Transform{.position = {1.f + 3.4f * static_cast<float>(i), 1.f, 0.f}});
            world.emplace<engine::BoxCollider2D>(body, engine::BoxCollider2D{.size = {0.9f, 0.9f, 1.f},
                                                                             .friction = 0.5f});
            world.emplace<engine::RigidBody2D>(body, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
            bodies.push_back(body);
        }
    }

    // The edit of tick `tick`; returns the entities it touched (empty for none).
    std::vector<engine::ecs::Entity> edit(int tick) {
        switch (tick % 50) {
        case 5: {
            world.get<engine::Transform>(cells[3]).position.y += 0.05f;
            return {cells[3]};
        }
        case 12: {
            world.get<engine::Transform>(parent).position.y += 0.03f;
            return {parent};
        }
        case 18: {
            auto& shapes = world.get<engine::CompoundCollider2D>(chunk).shapes;
            shapes.push_back(engine::box_shape_2d({8.5f + static_cast<float>(tick / 50), -0.5f}, {1.f, 1.f}));
            return {chunk};
        }
        case 25: {
            world.get<engine::CompoundCollider2D>(chunk).shapes[2] = engine::box_shape_2d({2.5f, -0.4f}, {1.f, 1.f});
            world.get<engine::BoxCollider2D>(cells[7]).size.x = 1.1f;
            return {chunk, cells[7]};
        }
        case 31: {
            world.get<engine::Transform>(sensor).position.x += 2.f;
            bool& is_trigger = world.get<engine::BoxCollider2D>(cells[9]).is_trigger;
            is_trigger = !is_trigger;
            return {sensor, cells[9]};
        }
        case 40: {
            auto& shapes = world.get<engine::CompoundCollider2D>(chunk).shapes;
            shapes.erase(shapes.begin() + 1);
            world.get<engine::Transform>(children[1]).rotation = engine::rotation_z(0.05f * static_cast<float>(tick));
            return {chunk, children[1]};
        }
        default:
            return {};
        }
    }
};

[[nodiscard]] bool same_bits(glm::vec3 a, glm::vec3 b) {
    return std::bit_cast<std::uint32_t>(a.x) == std::bit_cast<std::uint32_t>(b.x) &&
           std::bit_cast<std::uint32_t>(a.y) == std::bit_cast<std::uint32_t>(b.y) &&
           std::bit_cast<std::uint32_t>(a.z) == std::bit_cast<std::uint32_t>(b.z);
}

}

TEST(Physics2DStaticDirty, EntityMarksGiveTheSameTicksAsWorldMarks) {
    DeterminismScene by_entity;
    DeterminismScene by_world;
    int edits = 0;
    for (int tick = 0; tick < 200; ++tick) {
        const std::vector<engine::ecs::Entity> touched = by_entity.edit(tick);
        (void) by_world.edit(tick);
        for (engine::ecs::Entity entity : touched) {
            engine::mark_static_colliders_dirty_2d(by_entity.world, entity);
        }
        if (!touched.empty()) {
            engine::mark_static_colliders_dirty_2d(by_world.world);
            ++edits;
        }
        const std::vector<engine::CollisionEvent2D> a = step_and_read(by_entity.world);
        const std::vector<engine::CollisionEvent2D> b = step_and_read(by_world.world);
        ASSERT_EQ(a, b) << "tick " << tick;
        for (std::size_t i = 0; i < by_entity.bodies.size(); ++i) {
            const engine::Transform& ta = by_entity.world.get<engine::Transform>(by_entity.bodies[i]);
            const engine::Transform& tb = by_world.world.get<engine::Transform>(by_world.bodies[i]);
            const engine::RigidBody2D& ra = by_entity.world.get<engine::RigidBody2D>(by_entity.bodies[i]);
            const engine::RigidBody2D& rb = by_world.world.get<engine::RigidBody2D>(by_world.bodies[i]);
            ASSERT_TRUE(same_bits(ta.position, tb.position)) << "tick " << tick << " body " << i;
            ASSERT_TRUE(same_bits(ra.velocity, rb.velocity)) << "tick " << tick << " body " << i;
            ASSERT_EQ(std::bit_cast<std::uint32_t>(ra.angular_velocity),
                      std::bit_cast<std::uint32_t>(rb.angular_velocity));
        }

        // The cache itself: the same keys in the same order, the same items behind them, the same triggers.
        const engine::PhysicsBroadphase2D& ca = cache_of(by_entity.world);
        const engine::PhysicsBroadphase2D& cb = cache_of(by_world.world);
        ASSERT_EQ(ca.static_keys.size(), cb.static_keys.size()) << "tick " << tick;
        for (std::size_t k = 0; k < ca.static_keys.size(); ++k) {
            const engine::PhysicsBroadphase2D::Key& ka = ca.static_keys[k];
            const engine::PhysicsBroadphase2D::Key& kb = cb.static_keys[k];
            ASSERT_EQ(ka.entity, kb.entity);
            ASSERT_EQ(ka.shape, kb.shape);
            ASSERT_EQ(ka.part, kb.part);
            const engine::ColliderItem2D& ia = ca.static_slots[static_cast<std::size_t>(ka.slot)].item;
            const engine::ColliderItem2D& ib = cb.static_slots[static_cast<std::size_t>(kb.slot)].item;
            ASSERT_TRUE(same_bits(ia.position, ib.position)) << "tick " << tick;
            ASSERT_EQ(ia.polygon_count, ib.polygon_count);
            ASSERT_EQ(ia.is_trigger, ib.is_trigger);
        }
        ASSERT_EQ(ca.static_entities, cb.static_entities) << "tick " << tick;
        ASSERT_EQ(ca.static_triggers.size(), cb.static_triggers.size()) << "tick " << tick;
        for (std::size_t t = 0; t < ca.static_triggers.size(); ++t) {
            ASSERT_EQ(ca.static_triggers[t].entity, cb.static_triggers[t].entity);
            ASSERT_EQ(ca.static_triggers[t].shape, cb.static_triggers[t].shape);
            ASSERT_TRUE(same_bits(ca.static_triggers[t].position, cb.static_triggers[t].position));
        }
    }
    EXPECT_GT(edits, 0);
    // The per-entity world never needed a full re-read after the first tick.
    EXPECT_EQ(cache_of(by_entity.world).rebuild_count, 1u);
    EXPECT_EQ(cache_of(by_entity.world).entity_rebuild_count, static_cast<std::uint64_t>(edits));
}
