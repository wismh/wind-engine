#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <vector>

namespace {

void step_physics(engine::ecs::World& world) {
    world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    world.run(engine::ecs::Schedule::Fixed);
}

std::vector<engine::CollisionEvent2D> read_collisions(engine::ecs::World& world) {
    std::vector<engine::CollisionEvent2D> events;
    for (const engine::CollisionEvent2D& event : engine::ecs::EventReader<engine::CollisionEvent2D>{world}) {
        events.push_back(event);
    }
    return events;
}

void drop_collision_history(engine::ecs::World& world) {
    world.flush_events();
    world.flush_events();
}

engine::ecs::World physics_world() {
    engine::ecs::World world;
    engine::register_engine_systems(world);
    return world;
}

void expect_contact(const engine::CollisionEvent2D& event, glm::vec2 normal, float penetration, glm::vec2 point) {
    EXPECT_NEAR(event.normal.x, normal.x, 1e-5f);
    EXPECT_NEAR(event.normal.y, normal.y, 1e-5f);
    EXPECT_NEAR(event.penetration, penetration, 1e-5f);
    EXPECT_NEAR(event.point.x, point.x, 1e-5f);
    EXPECT_NEAR(event.point.y, point.y, 1e-5f);
}

engine::ecs::Entity add_box(engine::ecs::World& world, glm::vec3 position, glm::vec3 size, bool trigger = false) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity,
            engine::BoxCollider2D{.size = size, .is_trigger = trigger});
    return entity;
}

engine::ecs::Entity add_circle(engine::ecs::World& world, glm::vec3 position, float radius) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::CircleCollider2D>(entity, engine::CircleCollider2D{.radius = radius});
    return entity;
}

}

TEST(Physics2D, AabbNormalIsThinnerAxis) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity a = add_box(world, glm::vec3{0.f}, glm::vec3{4.f, 2.f, 1.f});
    const engine::ecs::Entity b = add_box(world, glm::vec3{3.f, 0.5f, 0.f}, glm::vec3{4.f, 2.f, 1.f});
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> x_thin = read_collisions(world);
    ASSERT_EQ(x_thin.size(), 1u);
    EXPECT_EQ(x_thin[0].a, a);
    EXPECT_EQ(x_thin[0].b, b);
    expect_contact(x_thin[0], glm::vec2{1.f, 0.f}, 1.f, glm::vec2{1.5f, 0.25f});

    engine::ecs::World y_world = physics_world();
    add_box(y_world, glm::vec3{0.f}, glm::vec3{4.f, 2.f, 1.f});
    add_box(y_world, glm::vec3{0.5f, 1.5f, 0.f}, glm::vec3{4.f, 2.f, 1.f});
    step_physics(y_world);
    const std::vector<engine::CollisionEvent2D> y_thin = read_collisions(y_world);
    ASSERT_EQ(y_thin.size(), 1u);
    expect_contact(y_thin[0], glm::vec2{0.f, 1.f}, 0.5f, glm::vec2{0.25f, 0.75f});

    engine::ecs::World aligned = physics_world();
    add_box(aligned, glm::vec3{0.f}, glm::vec3{2.f, 6.f, 1.f});
    add_box(aligned, glm::vec3{0.f, 1.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(aligned);
    const std::vector<engine::CollisionEvent2D> positive = read_collisions(aligned);
    ASSERT_EQ(positive.size(), 1u);
    expect_contact(positive[0], glm::vec2{1.f, 0.f}, 2.f, glm::vec2{0.f, 1.f});
}

TEST(Physics2D, AabbEqualPenetrationPrefersX) {
    engine::ecs::World world = physics_world();
    add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    add_box(world, glm::vec3{0.5f, 0.5f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    expect_contact(events[0], glm::vec2{1.f, 0.f}, 1.5f, glm::vec2{0.25f, 0.25f});
}

TEST(Physics2D, CircleContact) {
    engine::ecs::World world = physics_world();
    add_circle(world, glm::vec3{0.f}, 2.f);
    add_circle(world, glm::vec3{3.f, 4.f, 0.f}, 4.f);
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    expect_contact(events[0], glm::vec2{0.6f, 0.8f}, 1.f, glm::vec2{0.9f, 1.2f});
    const float length_sq = events[0].normal.x * events[0].normal.x + events[0].normal.y * events[0].normal.y;
    EXPECT_NEAR(length_sq, 1.f, 1e-5f);
}

TEST(Physics2D, BoxCircleSide) {
    engine::ecs::World world = physics_world();
    add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    add_circle(world, glm::vec3{1.5f, 0.25f, 0.f}, 1.f);
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    expect_contact(events[0], glm::vec2{1.f, 0.f}, 0.5f, glm::vec2{0.75f, 0.25f});
}

TEST(Physics2D, BoxCircleNormalFollowsEntityOrder) {
    engine::ecs::World world = physics_world();
    add_circle(world, glm::vec3{1.5f, 0.25f, 0.f}, 1.f);
    add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    expect_contact(events[0], glm::vec2{-1.f, 0.f}, 0.5f, glm::vec2{0.75f, 0.25f});
}

TEST(Physics2D, BoxCircleCorner) {
    engine::ecs::World world = physics_world();
    add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    add_circle(world, glm::vec3{2.f, 2.f, 0.f}, 2.f);
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    const float root = std::sqrt(2.f);
    expect_contact(events[0], glm::vec2{1.f / root, 1.f / root}, 2.f - root,
            glm::vec2{1.5f - root * 0.5f, 1.5f - root * 0.5f});
}

TEST(Physics2D, BoxCircleCenterInsideNearestFace) {
    engine::ecs::World world = physics_world();
    add_box(world, glm::vec3{0.f}, glm::vec3{4.f, 2.f, 1.f});
    add_circle(world, glm::vec3{0.5f, 0.f, 0.f}, 0.25f);
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    expect_contact(events[0], glm::vec2{0.f, 1.f}, 1.25f, glm::vec2{0.5f, 0.375f});
}

TEST(Physics2D, CoincidentCentersAreFinite) {
    {
        engine::ecs::World world = physics_world();
        add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
        add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
        step_physics(world);
        const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
        ASSERT_EQ(events.size(), 1u);
        expect_contact(events[0], glm::vec2{1.f, 0.f}, 2.f, glm::vec2{0.f, 0.f});
    }
    {
        engine::ecs::World world = physics_world();
        add_circle(world, glm::vec3{0.f}, 1.f);
        add_circle(world, glm::vec3{0.f}, 1.f);
        step_physics(world);
        const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
        ASSERT_EQ(events.size(), 1u);
        expect_contact(events[0], glm::vec2{1.f, 0.f}, 2.f, glm::vec2{0.f, 0.f});
    }
    {
        engine::ecs::World world = physics_world();
        add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 4.f, 1.f});
        add_circle(world, glm::vec3{0.f}, 0.5f);
        step_physics(world);
        const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
        ASSERT_EQ(events.size(), 1u);
        expect_contact(events[0], glm::vec2{1.f, 0.f}, 1.5f, glm::vec2{0.25f, 0.f});
    }
}

TEST(Physics2D, TriggerContactDoesNotMove) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity a = add_box(world, glm::vec3{0.f}, glm::vec3{4.f, 2.f, 1.f}, true);
    const engine::ecs::Entity b = add_box(world, glm::vec3{3.f, 0.5f, 0.f}, glm::vec3{4.f, 2.f, 1.f});
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].is_trigger);
    expect_contact(events[0], glm::vec2{1.f, 0.f}, 1.f, glm::vec2{1.5f, 0.25f});
    EXPECT_EQ(world.get<engine::Transform>(a).position, (glm::vec3{0.f}));
    EXPECT_EQ(world.get<engine::Transform>(b).position, (glm::vec3{3.f, 0.5f, 0.f}));
}

TEST(Physics2D, ExitContactIsZero) {
    engine::ecs::World world = physics_world();
    add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    const engine::ecs::Entity b = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(world);
    world.get<engine::Transform>(b).position.x = 100.f;
    drop_collision_history(world);
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].phase, engine::CollisionPhase::Exit);
    EXPECT_FALSE(events[0].is_trigger);
    expect_contact(events[0], glm::vec2{0.f}, 0.f, glm::vec2{0.f});

    drop_collision_history(world);
    step_physics(world);
    EXPECT_TRUE(read_collisions(world).empty());

    engine::ecs::World triggers = physics_world();
    add_box(triggers, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f}, true);
    const engine::ecs::Entity other = add_box(triggers, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(triggers);
    triggers.get<engine::Transform>(other).position.x = 100.f;
    drop_collision_history(triggers);
    step_physics(triggers);
    const std::vector<engine::CollisionEvent2D> trigger_exit = read_collisions(triggers);
    ASSERT_EQ(trigger_exit.size(), 1u);
    EXPECT_EQ(trigger_exit[0].phase, engine::CollisionPhase::Exit);
    EXPECT_TRUE(trigger_exit[0].is_trigger);
    expect_contact(trigger_exit[0], glm::vec2{0.f}, 0.f, glm::vec2{0.f});
}

TEST(Physics2D, SeparatedPairsEmitNothing) {
    engine::ecs::World boxes = physics_world();
    add_box(boxes, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    add_box(boxes, glm::vec3{3.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(boxes);
    EXPECT_TRUE(read_collisions(boxes).empty());

    engine::ecs::World mixed = physics_world();
    add_box(mixed, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    add_circle(mixed, glm::vec3{3.f, 0.f, 0.f}, 1.f);
    step_physics(mixed);
    EXPECT_TRUE(read_collisions(mixed).empty());
}

TEST(Physics2D, TouchingPairsHaveZeroPenetration) {
    engine::ecs::World boxes = physics_world();
    add_box(boxes, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    add_box(boxes, glm::vec3{2.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(boxes);
    const std::vector<engine::CollisionEvent2D> box_events = read_collisions(boxes);
    ASSERT_EQ(box_events.size(), 1u);
    expect_contact(box_events[0], glm::vec2{1.f, 0.f}, 0.f, glm::vec2{1.f, 0.f});

    engine::ecs::World circles = physics_world();
    add_circle(circles, glm::vec3{0.f}, 1.f);
    add_circle(circles, glm::vec3{2.f, 0.f, 0.f}, 1.f);
    step_physics(circles);
    const std::vector<engine::CollisionEvent2D> circle_events = read_collisions(circles);
    ASSERT_EQ(circle_events.size(), 1u);
    expect_contact(circle_events[0], glm::vec2{1.f, 0.f}, 0.f, glm::vec2{1.f, 0.f});

    engine::ecs::World mixed = physics_world();
    add_box(mixed, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    add_circle(mixed, glm::vec3{2.f, 0.f, 0.f}, 1.f);
    step_physics(mixed);
    const std::vector<engine::CollisionEvent2D> mixed_events = read_collisions(mixed);
    ASSERT_EQ(mixed_events.size(), 1u);
    expect_contact(mixed_events[0], glm::vec2{1.f, 0.f}, 0.f, glm::vec2{1.f, 0.f});
}

TEST(Physics2D, ChildColliderContactUsesWorldPosition) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity parent = world.create();
    world.emplace<engine::Transform>(parent, engine::Transform{.position = {10.f, 0.f, 0.f}});
    const engine::ecs::Entity child = world.create();
    world.emplace<engine::Transform>(child, engine::Transform{.position = {0.5f, 0.f, 0.f}});
    world.emplace<engine::BoxCollider2D>(child, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});
    ASSERT_TRUE(engine::ecs::set_parent(world, child, parent));
    const engine::ecs::Entity other = add_box(world, glm::vec3{10.5f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    add_box(world, glm::vec3{0.5f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, child);
    EXPECT_EQ(events[0].b, other);
    expect_contact(events[0], glm::vec2{1.f, 0.f}, 2.f, glm::vec2{10.5f, 0.f});
}

TEST(Physics2D, IdentityRotationBoxMatchesLegacyAabb) {
    engine::ecs::World thin = physics_world();
    const engine::ecs::Entity a = add_box(thin, glm::vec3{0.f}, glm::vec3{4.f, 2.f, 1.f});
    const engine::ecs::Entity b = add_box(thin, glm::vec3{3.f, 0.5f, 0.f}, glm::vec3{4.f, 2.f, 1.f});
    thin.get<engine::Transform>(a).rotation = engine::rotation_z(0.f);
    thin.get<engine::Transform>(b).rotation = engine::rotation_z(0.f);
    step_physics(thin);
    const std::vector<engine::CollisionEvent2D> thin_events = read_collisions(thin);
    ASSERT_EQ(thin_events.size(), 1u);
    expect_contact(thin_events[0], glm::vec2{1.f, 0.f}, 1.f, glm::vec2{1.5f, 0.25f});

    engine::ecs::World coincident = physics_world();
    const engine::ecs::Entity left = add_box(coincident, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    const engine::ecs::Entity right = add_box(coincident, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    coincident.get<engine::Transform>(left).rotation = engine::rotation_z(0.f);
    coincident.get<engine::Transform>(right).rotation = engine::rotation_z(0.f);
    step_physics(coincident);
    const std::vector<engine::CollisionEvent2D> coincident_events = read_collisions(coincident);
    ASSERT_EQ(coincident_events.size(), 1u);
    expect_contact(coincident_events[0], glm::vec2{1.f, 0.f}, 2.f, glm::vec2{0.f, 0.f});
}

TEST(Physics2D, ContactUsesWorldYawAndIgnoresScaleAndZ) {
    engine::ecs::World rotated = physics_world();
    const engine::ecs::Entity parent = rotated.create();
    rotated.emplace<engine::Transform>(parent, engine::Transform{
            .position = {0.f, 0.f, 0.f},
            .rotation = engine::rotation_z(glm::half_pi<float>()),
    });
    const engine::ecs::Entity child = rotated.create();
    rotated.emplace<engine::Transform>(child, engine::Transform{.position = {2.f, 0.f, 0.f}});
    rotated.emplace<engine::BoxCollider2D>(child, engine::BoxCollider2D{glm::vec3{4.f, 2.f, 1.f}});
    ASSERT_TRUE(engine::ecs::set_parent(rotated, child, parent));
    add_box(rotated, glm::vec3{0.f, 3.25f, 50.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(rotated);
    const std::vector<engine::CollisionEvent2D> rotated_events = read_collisions(rotated);
    ASSERT_EQ(rotated_events.size(), 1u);
    expect_contact(rotated_events[0], glm::vec2{0.f, 1.f}, 1.75f, glm::vec2{0.f, 2.25f});

    engine::ecs::World scaled = physics_world();
    const engine::ecs::Entity scale_parent = scaled.create();
    scaled.emplace<engine::Transform>(scale_parent, engine::Transform{
            .position = {0.f, 0.f, 0.f},
            .scale = {2.f, 2.f, 1.f},
    });
    const engine::ecs::Entity scale_child = scaled.create();
    scaled.emplace<engine::Transform>(scale_child, engine::Transform{.position = {1.f, 0.f, 0.f}});
    scaled.emplace<engine::BoxCollider2D>(scale_child, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});
    ASSERT_TRUE(engine::ecs::set_parent(scaled, scale_child, scale_parent));
    add_box(scaled, glm::vec3{2.5f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(scaled);
    const std::vector<engine::CollisionEvent2D> scaled_events = read_collisions(scaled);
    ASSERT_EQ(scaled_events.size(), 1u);
    expect_contact(scaled_events[0], glm::vec2{1.f, 0.f}, 1.5f, glm::vec2{2.25f, 0.f});
}

TEST(Physics2D, StayContactTracksCurrentOverlap) {
    engine::ecs::World world = physics_world();
    add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    const engine::ecs::Entity b = add_box(world, glm::vec3{1.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> entered = read_collisions(world);
    ASSERT_EQ(entered.size(), 1u);
    EXPECT_EQ(entered[0].phase, engine::CollisionPhase::Enter);
    expect_contact(entered[0], glm::vec2{1.f, 0.f}, 1.f, glm::vec2{0.5f, 0.f});

    world.get<engine::Transform>(b).position.x = 0.5f;
    drop_collision_history(world);
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> stayed = read_collisions(world);
    ASSERT_EQ(stayed.size(), 1u);
    EXPECT_EQ(stayed[0].phase, engine::CollisionPhase::Stay);
    expect_contact(stayed[0], glm::vec2{1.f, 0.f}, 1.5f, glm::vec2{0.25f, 0.f});
}

TEST(Physics2D, OverlapDoesNotChangeMotion) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity a = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::RigidBody2D>(a, engine::RigidBody2D{glm::vec3{60.f, 0.f, 0.f}});
    world.emplace<engine::BoxCollider2D>(a, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});
    const engine::ecs::Entity b = add_box(world, glm::vec3{1.5f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(world);

    EXPECT_NEAR(world.get<engine::Transform>(a).position.x, 1.f, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody2D>(a).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    EXPECT_EQ(world.get<engine::Transform>(b).position, (glm::vec3{1.5f, 0.f, 0.f}));
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    expect_contact(events[0], glm::vec2{1.f, 0.f}, 1.5f, glm::vec2{1.25f, 0.f});
}

TEST(Physics2D, TwoCollidersOnOneBodyPublishOneEvent) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity body = world.create();
    world.emplace<engine::Transform>(body, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(body, engine::BoxCollider2D{.size = {2.f, 2.f, 1.f}});
    world.emplace<engine::CircleCollider2D>(body, engine::CircleCollider2D{.radius = 1.f});
    const engine::ecs::Entity other = add_box(world, glm::vec3{1.5f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, body);
    EXPECT_EQ(events[0].b, other);
    EXPECT_EQ(events[0].phase, engine::CollisionPhase::Enter);
}

TEST(Physics2D, FirstShapePairKeepsItsManifold) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity body = world.create();
    world.emplace<engine::Transform>(body, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(body, engine::BoxCollider2D{.size = {2.f, 4.f, 1.f}});
    world.emplace<engine::CircleCollider2D>(body, engine::CircleCollider2D{.radius = 1.5f});
    const engine::ecs::Entity other = add_box(world, glm::vec3{1.2f, 0.f, 0.f}, glm::vec3{1.f, 1.f, 1.f});

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, body);
    EXPECT_EQ(events[0].b, other);
    // The circle overlap is 0.8 deep. The box, gathered first, is the 0.3 face.
    expect_contact(events[0], glm::vec2{1.f, 0.f}, 0.3f, glm::vec2{0.85f, 0.f});
}
