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
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

// LQ-23: CompoundCollider2D, many static shapes on one entity.

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

// A kinematic box: active, moves only with `velocity`.
engine::ecs::Entity add_probe(engine::ecs::World& world, glm::vec3 position, glm::vec3 size = {2.f, 2.f, 1.f},
                              glm::vec3 velocity = glm::vec3{0.f}) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = size});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{.velocity = velocity});
    return entity;
}

engine::ecs::Entity add_compound(engine::ecs::World& world, glm::vec3 position,
                                 std::vector<engine::CompoundShape2D> shapes) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::CompoundCollider2D>(entity, engine::CompoundCollider2D{.shapes = std::move(shapes)});
    return entity;
}

// `count` unit boxes in a row, centers at (first_x + i, y).
std::vector<engine::CompoundShape2D> box_row(int count, float first_x, float y) {
    std::vector<engine::CompoundShape2D> shapes;
    for (int i = 0; i < count; ++i) {
        shapes.push_back(engine::box_shape_2d({first_x + static_cast<float>(i), y}, {1.f, 1.f}));
    }
    return shapes;
}

[[nodiscard]] int count_events(const std::vector<engine::CollisionEvent2D>& events, engine::ecs::Entity a,
                               engine::ecs::Entity b, engine::CollisionPhase phase) {
    return static_cast<int>(std::ranges::count_if(events, [&](const engine::CollisionEvent2D& event) {
        const bool same_pair = (event.a == a && event.b == b) || (event.a == b && event.b == a);
        return same_pair && event.phase == phase;
    }));
}

[[nodiscard]] bool has_event(const std::vector<engine::CollisionEvent2D>& events, engine::ecs::Entity a,
                             engine::ecs::Entity b, engine::CollisionPhase phase) {
    return count_events(events, a, b, phase) > 0;
}

[[nodiscard]] std::uint64_t rebuild_count(engine::ecs::World& world) {
    return world.ctx<engine::PhysicsBroadphase2D>().rebuild_count;
}

[[nodiscard]] bool contains(const std::vector<engine::ecs::Entity>& sorted, engine::ecs::Entity entity) {
    return std::binary_search(sorted.begin(), sorted.end(), entity);
}

}

TEST(Physics2DCompound, BoxShapeIsCounterClockwise) {
    const engine::CompoundShape2D shape = engine::box_shape_2d({1.f, 2.f}, {4.f, 2.f}, 0.5f);
    ASSERT_EQ(shape.count, 4);
    float area = 0.f;
    glm::vec2 center{0.f};
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 a = shape.vertices[i];
        const glm::vec2 b = shape.vertices[(i + 1) % 4];
        area += a.x * b.y - b.x * a.y;
        center += a * 0.25f;
    }
    EXPECT_NEAR(area * 0.5f, 8.f, 1e-4f);
    EXPECT_NEAR(center.x, 1.f, 1e-5f);
    EXPECT_NEAR(center.y, 2.f, 1e-5f);
}

TEST(Physics2DCompound, EveryShapeIsInTheStaticTree) {
    engine::ecs::World world = physics_world();
    std::vector<engine::CompoundShape2D> shapes;
    for (int i = 0; i < 40; ++i) {
        shapes.push_back(engine::box_shape_2d({10.f * static_cast<float>(i), 0.f}, {2.f, 2.f}));
    }
    const engine::ecs::Entity chunk = add_compound(world, glm::vec3{0.f}, std::move(shapes));
    const engine::ecs::Entity first = add_probe(world, glm::vec3{1.5f, 0.f, 0.f});
    const engine::ecs::Entity middle = add_probe(world, glm::vec3{201.5f, 0.f, 0.f});
    const engine::ecs::Entity last = add_probe(world, glm::vec3{391.5f, 0.f, 0.f});
    const engine::ecs::Entity between = add_probe(world, glm::vec3{5.f, 0.f, 0.f});

    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, first, chunk, engine::CollisionPhase::Enter));
    EXPECT_TRUE(has_event(events, middle, chunk, engine::CollisionPhase::Enter));
    EXPECT_TRUE(has_event(events, last, chunk, engine::CollisionPhase::Enter));
    EXPECT_FALSE(has_event(events, between, chunk, engine::CollisionPhase::Enter));
    EXPECT_EQ(world.ctx<engine::PhysicsBroadphase2D>().static_keys.size(), 40u);
    EXPECT_EQ(world.ctx<engine::PhysicsBroadphase2D>().static_entities.size(), 1u);
}

TEST(Physics2DCompound, DynamicBoxesRestOnShapesAndSeams) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -20.f, 0.f};
    // Ten unit boxes, top at y = 0, x in [-5, 5], plus a ramp triangle to the right.
    std::vector<engine::CompoundShape2D> shapes = box_row(10, -4.5f, -0.5f);
    engine::CompoundShape2D wedge;
    wedge.vertices[0] = {5.f, -1.f};
    wedge.vertices[1] = {9.f, -1.f};
    wedge.vertices[2] = {9.f, 0.f};
    wedge.count = 3;
    shapes.push_back(wedge);
    (void)add_compound(world, glm::vec3{0.f}, std::move(shapes));

    std::vector<engine::ecs::Entity> crates;
    for (const float x : {0.f, 2.3f, -3.5f}) {
        const engine::ecs::Entity crate = world.create();
        world.emplace<engine::Transform>(crate, engine::Transform{.position = {x, 2.f, 0.f}});
        world.emplace<engine::BoxCollider2D>(crate, engine::BoxCollider2D{.size = {1.f, 1.f, 1.f}, .friction = 0.5f});
        world.emplace<engine::RigidBody2D>(crate, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
        crates.push_back(crate);
    }
    for (int i = 0; i < 240; ++i) {
        step_physics(world);
        world.flush_events();
    }
    for (const engine::ecs::Entity crate : crates) {
        const glm::vec3 position = world.get<engine::Transform>(crate).position;
        EXPECT_NEAR(position.y, 0.5f, 0.05f);
        EXPECT_LT(std::abs(world.get<engine::RigidBody2D>(crate).velocity.y), 0.1f);
    }
    EXPECT_NEAR(world.get<engine::Transform>(crates[0]).position.x, 0.f, 0.05f);
}

TEST(Physics2DCompound, ContactCacheIsPerShape) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -20.f, 0.f};
    const engine::ecs::Entity chunk = add_compound(world, glm::vec3{0.f}, box_row(10, -4.5f, -0.5f));
    // Straddles the seam at x = 0, between parts 4 and 5.
    const engine::ecs::Entity crate = world.create();
    world.emplace<engine::Transform>(crate, engine::Transform{.position = {0.f, 1.f, 0.f}});
    world.emplace<engine::BoxCollider2D>(crate, engine::BoxCollider2D{.size = {1.f, 1.f, 1.f}});
    world.emplace<engine::RigidBody2D>(crate, engine::RigidBody2D{.type = engine::BodyType::Dynamic});

    std::set<std::uint32_t> parts;
    for (int i = 0; i < 120 && parts.size() < 2; ++i) {
        step_physics(world);
        world.flush_events();
        parts.clear();
        for (const auto& [key, impulse] : world.ctx<engine::PhysicsContactCache2D>().impulses) {
            if (key.a == chunk && key.shape_a == static_cast<std::uint8_t>(engine::ColliderShape2D::Compound)) {
                parts.insert(key.part_a);
            }
            if (key.b == chunk && key.shape_b == static_cast<std::uint8_t>(engine::ColliderShape2D::Compound)) {
                parts.insert(key.part_b);
            }
        }
    }
    EXPECT_EQ(parts, (std::set<std::uint32_t>{4u, 5u}));
}

TEST(Physics2DCompound, OneEventPerEntityPairFromTheDeepestShape) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    // Part 0 is 0.2 deep into the probe, part 1 is 0.8 deep.
    const engine::ecs::Entity chunk = add_compound(
            world, glm::vec3{0.f},
            {engine::box_shape_2d({1.8f, 0.f}, {2.f, 2.f}), engine::box_shape_2d({1.2f, 0.f}, {2.f, 2.f})});
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, chunk, engine::CollisionPhase::Enter));
    EXPECT_NEAR(events[0].penetration, 0.8f, 1e-4f);
}

TEST(Physics2DCompound, SlidingAcrossShapesIsOneStayStream) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity chunk = add_compound(world, glm::vec3{0.f}, box_row(20, -9.5f, -0.5f));
    // Bottom at y = -0.1: always touching one or two parts while it slides 5 units.
    const engine::ecs::Entity probe =
            add_probe(world, glm::vec3{-5.f, 0.4f, 0.f}, glm::vec3{1.f, 1.f, 1.f}, glm::vec3{5.f, 0.f, 0.f});
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_EQ(count_events(events, probe, chunk, engine::CollisionPhase::Enter), 1);
    for (int i = 0; i < 60; ++i) {
        events = step_and_read(world);
        ASSERT_EQ(events.size(), 1u) << "tick " << i;
        EXPECT_TRUE(has_event(events, probe, chunk, engine::CollisionPhase::Stay));
    }
}

TEST(Physics2DCompound, ShapesFollowTheEntityAndItsParent) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity root = world.create();
    world.emplace<engine::Transform>(root, engine::Transform{});
    const engine::ecs::Entity chunk =
            add_compound(world, glm::vec3{0.f}, {engine::box_shape_2d({3.f, 0.f}, {1.f, 1.f})});
    ASSERT_TRUE(engine::ecs::set_parent(world, chunk, root));
    const engine::ecs::Entity east = add_probe(world, glm::vec3{3.f, 0.f, 0.f}, glm::vec3{0.5f, 0.5f, 1.f});
    const engine::ecs::Entity north = add_probe(world, glm::vec3{0.f, 3.f, 0.f}, glm::vec3{0.5f, 0.5f, 1.f});
    const engine::ecs::Entity moved = add_probe(world, glm::vec3{10.f, 3.f, 0.f}, glm::vec3{0.5f, 0.5f, 1.f});
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, east, chunk, engine::CollisionPhase::Enter));
    EXPECT_FALSE(has_event(events, north, chunk, engine::CollisionPhase::Enter));

    // A quarter turn about +Z puts the shape at (0, 3).
    world.get<engine::Transform>(chunk).rotation = engine::rotation_z(1.5707963f);
    engine::mark_static_colliders_dirty_2d(world, chunk);
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, east, chunk, engine::CollisionPhase::Exit));
    EXPECT_TRUE(has_event(events, north, chunk, engine::CollisionPhase::Enter));

    world.get<engine::Transform>(root).position.x = 10.f;
    engine::mark_static_colliders_dirty_2d(world, root);
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, north, chunk, engine::CollisionPhase::Exit));
    EXPECT_TRUE(has_event(events, moved, chunk, engine::CollisionPhase::Enter));
}

TEST(Physics2DCompound, LayerAndMaskApplyToEveryShape) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    world.get<engine::BoxCollider2D>(probe).mask = 1u;
    const engine::ecs::Entity chunk = add_compound(world, glm::vec3{0.f}, box_row(3, 0.f, 0.f));
    world.get<engine::CompoundCollider2D>(chunk).layer = 2u;
    EXPECT_TRUE(step_and_read(world).empty());

    world.get<engine::CompoundCollider2D>(chunk).layer = 3u;
    engine::mark_static_colliders_dirty_2d(world, chunk);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, chunk, engine::CollisionPhase::Enter));
}

TEST(Physics2DCompound, AddingAndRemovingNeedsNoMark) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    const engine::ecs::Entity chunk = world.create();
    world.emplace<engine::Transform>(chunk, engine::Transform{});
    EXPECT_TRUE(step_and_read(world).empty());

    world.emplace<engine::CompoundCollider2D>(chunk, engine::CompoundCollider2D{.shapes = box_row(2, 0.5f, 0.f)});
    std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, chunk, engine::CollisionPhase::Enter));

    world.remove<engine::CompoundCollider2D>(chunk);
    events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, chunk, engine::CollisionPhase::Exit));
}

TEST(Physics2DCompound, MarkedShapeEditAppliesNextTick) {
    engine::ecs::World world = physics_world();
    // 256 far shapes before the edited one, so only the mark (not the audit) can find it next tick.
    std::vector<engine::CompoundShape2D> shapes;
    for (int i = 0; i < 256; ++i) {
        shapes.push_back(engine::box_shape_2d({3.f * static_cast<float>(i), 1000.f}, {1.f, 1.f}));
    }
    shapes.push_back(engine::box_shape_2d({50.f, 0.f}, {1.f, 1.f}));
    const engine::ecs::Entity chunk = add_compound(world, glm::vec3{0.f}, std::move(shapes));
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
    EXPECT_TRUE(step_and_read(world).empty());

    world.get<engine::CompoundCollider2D>(chunk).shapes.back() = engine::box_shape_2d({1.f, 0.f}, {1.f, 1.f});
    engine::mark_static_colliders_dirty_2d(world, chunk);
    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(has_event(events, probe, chunk, engine::CollisionPhase::Enter));
}

TEST(Physics2DCompound, UnmarkedEditsAreFoundByTheAudit) {
    constexpr int kParts = 100;
    const int bound = (kParts + 31) / 32 + 1;  // 32 cached statics are re-read per tick
    enum class Edit { Vertex, Grow, Shrink };
    for (const Edit edit : {Edit::Vertex, Edit::Grow, Edit::Shrink}) {
        engine::ecs::World world = physics_world();
        const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});
        std::vector<engine::CompoundShape2D> shapes;
        for (int i = 0; i < kParts; ++i) {
            shapes.push_back(engine::box_shape_2d({100.f + 3.f * static_cast<float>(i), 0.f}, {1.f, 1.f}));
        }
        if (edit == Edit::Shrink) {
            shapes.push_back(engine::box_shape_2d({1.f, 0.f}, {1.f, 1.f}));
        }
        const engine::ecs::Entity chunk = add_compound(world, glm::vec3{0.f}, std::move(shapes));
        const bool touching = edit == Edit::Shrink;
        EXPECT_EQ(has_event(step_and_read(world), probe, chunk, engine::CollisionPhase::Enter), touching);
        const std::uint64_t rebuilds = rebuild_count(world);

        std::vector<engine::CompoundShape2D>& edited = world.get<engine::CompoundCollider2D>(chunk).shapes;
        if (edit == Edit::Vertex) {
            edited[kParts / 2] = engine::box_shape_2d({1.f, 0.f}, {1.f, 1.f});
        } else if (edit == Edit::Grow) {
            edited.push_back(engine::box_shape_2d({1.f, 0.f}, {1.f, 1.f}));
        } else {
            edited.pop_back();
        }
        const engine::CollisionPhase expected = touching ? engine::CollisionPhase::Exit : engine::CollisionPhase::Enter;
        int found_at = -1;
        for (int tick = 0; tick < bound && found_at < 0; ++tick) {
            if (has_event(step_and_read(world), probe, chunk, expected)) {
                found_at = tick;
            }
        }
        EXPECT_GE(found_at, 0) << "edit " << static_cast<int>(edit);
        EXPECT_EQ(rebuild_count(world), rebuilds + 1) << "edit " << static_cast<int>(edit);
        EXPECT_TRUE(contains(world.ctx<engine::PhysicsBroadphase2D>().audit_warned, chunk));
    }
}

TEST(Physics2DCompound, IdleTicksDoNotRebuild) {
    engine::ecs::World world = physics_world();
    (void)add_probe(world, glm::vec3{0.f, 10.f, 0.f});
    (void)add_compound(world, glm::vec3{0.f}, box_row(200, 0.f, 0.f));
    step_physics(world);
    const std::uint64_t rebuilds = rebuild_count(world);
    for (int i = 0; i < 100; ++i) {
        step_physics(world);
    }
    EXPECT_EQ(rebuild_count(world), rebuilds);
}

TEST(Physics2DCompound, KinematicCompoundPushesADynamicBox) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = glm::vec3{0.f, -20.f, 0.f};
    const engine::ecs::Entity lift = add_compound(world, glm::vec3{0.f}, box_row(4, -1.5f, -0.5f));
    world.emplace<engine::RigidBody2D>(lift, engine::RigidBody2D{.velocity = {0.f, 2.f, 0.f}});
    const engine::ecs::Entity crate = world.create();
    world.emplace<engine::Transform>(crate, engine::Transform{.position = {0.f, 0.5f, 0.f}});
    world.emplace<engine::BoxCollider2D>(crate, engine::BoxCollider2D{.size = {1.f, 1.f, 1.f}});
    world.emplace<engine::RigidBody2D>(crate, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    for (int i = 0; i < 60; ++i) {
        step_physics(world);
        world.flush_events();
    }
    const float lift_y = world.get<engine::Transform>(lift).position.y;
    EXPECT_GT(lift_y, 1.5f);
    EXPECT_NEAR(world.get<engine::Transform>(crate).position.y, lift_y + 0.5f, 0.1f);
    EXPECT_TRUE(world.ctx<engine::PhysicsBroadphase2D>().static_keys.empty());
}

TEST(Physics2DCompound, CompoundUnderADynamicBodyIsIgnoredAndWarned) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity wall = world.create();
    world.emplace<engine::Transform>(wall, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(wall, engine::BoxCollider2D{.size = {4.f, 4.f, 1.f}});
    const engine::ecs::Entity probe = add_probe(world, glm::vec3{0.f});

    // On the body's own entity, and on a child of a dynamic body.
    const engine::ecs::Entity own = add_compound(world, glm::vec3{0.f}, box_row(2, 0.f, 0.f));
    world.emplace<engine::RigidBody2D>(own, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    const engine::ecs::Entity carrier = world.create();
    world.emplace<engine::Transform>(carrier, engine::Transform{.position = {0.f, 20.f, 0.f}});
    world.emplace<engine::RigidBody2D>(carrier, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    const engine::ecs::Entity child = add_compound(world, glm::vec3{0.f, -20.f, 0.f}, box_row(2, 0.f, 0.f));
    ASSERT_TRUE(engine::ecs::set_parent(world, child, carrier));

    const std::vector<engine::CollisionEvent2D> events = step_and_read(world);
    EXPECT_TRUE(has_event(events, probe, wall, engine::CollisionPhase::Enter));
    for (const engine::ecs::Entity ignored : {own, child}) {
        EXPECT_FALSE(has_event(events, probe, ignored, engine::CollisionPhase::Enter));
        EXPECT_FALSE(has_event(events, wall, ignored, engine::CollisionPhase::Enter));
        EXPECT_TRUE(contains(world.ctx<engine::PhysicsBroadphase2D>().dynamic_terrain_warned, ignored));
        EXPECT_FALSE(contains(world.ctx<engine::PhysicsBroadphase2D>().static_entities, ignored));
    }
    EXPECT_EQ(world.ctx<engine::PhysicsBroadphase2D>().dynamic_terrain_warned.size(), 2u);

    // Retyped to Static: the compound becomes static terrain with no mark.
    world.get<engine::RigidBody2D>(own).type = engine::BodyType::Static;
    const std::vector<engine::CollisionEvent2D> after = step_and_read(world);
    EXPECT_TRUE(has_event(after, probe, own, engine::CollisionPhase::Enter));
}
