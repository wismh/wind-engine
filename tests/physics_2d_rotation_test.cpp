#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>

#include <glm/gtc/constants.hpp>

#include "ecs/inertia_2d.h"

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

engine::ecs::World physics_world() {
    engine::ecs::World world;
    engine::register_engine_systems(world);
    return world;
}

engine::ecs::Entity add_box(engine::ecs::World& world, glm::vec3 position, glm::vec3 size) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = size});
    return entity;
}

void make_dynamic(engine::ecs::World& world, engine::ecs::Entity entity, glm::vec3 velocity, float mass,
                  float angular_velocity = 0.f) {
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{
            .velocity = velocity,
            .type = engine::BodyType::Dynamic,
            .mass = mass,
            .angular_velocity = angular_velocity,
    });
}

}

TEST(Physics2D, DynamicRootYawsWithoutContact) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = {2.f, 2.f, 1.f}});
    make_dynamic(world, entity, glm::vec3{60.f, 30.f, 0.f}, 1.f, 2.f);

    step_physics(world);

    const glm::vec3 position = world.get<engine::Transform>(entity).position;
    EXPECT_NEAR(position.x, 1.f, 1e-5f);
    EXPECT_NEAR(position.y, 0.5f, 1e-5f);
    EXPECT_NEAR(position.z, 0.f, 1e-5f);
    EXPECT_NEAR(engine::angle_z(world.get<engine::Transform>(entity).rotation), 2.f * engine::kFixed, 1e-5f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(entity).angular_velocity, 2.f, 1e-6f);
}

TEST(Physics2D, DynamicChildYawsAboutWorldZ) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity parent = world.create();
    world.emplace<engine::Transform>(parent, engine::Transform{
            .position = {10.f, 0.f, 0.f},
            .rotation = engine::rotation_z(glm::half_pi<float>()),
    });
    const engine::ecs::Entity child = world.create();
    world.emplace<engine::Transform>(child, engine::Transform{});
    make_dynamic(world, child, glm::vec3{0.f}, 1.f, 2.f);
    ASSERT_TRUE(engine::ecs::set_parent(world, child, parent));

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, child).x, 10.f, 1e-4f);
    EXPECT_NEAR(engine::ecs::world_position(world, child).y, 0.f, 1e-4f);
    EXPECT_NEAR(engine::angle_z(world.get<engine::Transform>(child).rotation), 2.f * engine::kFixed, 1e-4f);
    const glm::mat4 world_matrix = engine::ecs::compute_world_matrix(world, child);
    const float yaw = std::atan2(world_matrix[0][1], world_matrix[0][0]);
    EXPECT_NEAR(yaw, glm::half_pi<float>() + 2.f * engine::kFixed, 1e-4f);
}

TEST(Physics2D, KinematicAngularVelocityIsStoredNotApplied) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity platform = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.emplace<engine::RigidBody2D>(platform, engine::RigidBody2D{
            .velocity = {60.f, 0.f, 0.f},
            .angular_velocity = 4.f,
    });
    const engine::ecs::Entity crate = add_box(world, glm::vec3{2.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, crate, glm::vec3{0.f}, 1.f);

    step_physics(world);

    EXPECT_NEAR(world.get<engine::Transform>(platform).position.x, 1.f, 1e-5f);
    EXPECT_NEAR(engine::angle_z(world.get<engine::Transform>(platform).rotation), 0.f, 1e-6f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(platform).angular_velocity, 4.f, 1e-6f);
    EXPECT_NEAR(engine::ecs::world_position(world, crate).x, 2.999f, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(crate).velocity.x, 60.f, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(crate).angular_velocity, 0.f, 1e-4f);
}

TEST(Physics2D, OrientedBoxContactMatchesSat) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity diamond = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.get<engine::Transform>(diamond).rotation = engine::rotation_z(glm::quarter_pi<float>());
    add_box(world, glm::vec3{2.f, 0.f, 0.f}, glm::vec3{2.f, 4.f, 1.f});
    world.emplace<engine::RigidBody2D>(diamond, engine::RigidBody2D{});

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    const float root = std::sqrt(2.f);
    EXPECT_NEAR(events[0].normal.x, 1.f, 1e-4f);
    EXPECT_NEAR(events[0].normal.y, 0.f, 1e-4f);
    EXPECT_NEAR(events[0].penetration, root - 1.f, 1e-4f);
    EXPECT_NEAR(events[0].point.x, root, 1e-4f);
    EXPECT_NEAR(events[0].point.y, 0.f, 1e-4f);
}

TEST(Physics2D, CircleContactIgnoresSpin) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity box = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    const engine::ecs::Entity circle = world.create();
    world.emplace<engine::Transform>(circle, engine::Transform{
            .position = {1.5f, 0.25f, 0.f},
            .rotation = engine::rotation_z(glm::half_pi<float>()),
    });
    world.emplace<engine::CircleCollider2D>(circle, engine::CircleCollider2D{.radius = 1.f});
    make_dynamic(world, circle, glm::vec3{0.f}, 1.f, 3.f);

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].normal.x, 1.f, 1e-5f);
    EXPECT_NEAR(events[0].normal.y, 0.f, 1e-5f);
    EXPECT_NEAR(events[0].penetration, 0.5f, 1e-5f);
    EXPECT_NEAR(events[0].point.x, 0.75f, 1e-5f);
    EXPECT_NEAR(events[0].point.y, 0.25f, 1e-5f);
    EXPECT_EQ(world.get<engine::Transform>(box).position, (glm::vec3{0.f}));
}

TEST(Physics2D, DynamicBoxSlidesDownStaticSlope) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity slope = world.create();
    world.emplace<engine::Transform>(slope, engine::Transform{});
    engine::PolygonCollider2D polygon{};
    polygon.count = 3;
    polygon.vertices[0] = {0.f, 0.f};
    polygon.vertices[1] = {4.f, 0.f};
    polygon.vertices[2] = {4.f, 2.f};
    world.emplace<engine::PolygonCollider2D>(slope, polygon);
    world.emplace<engine::RigidBody2D>(slope, engine::RigidBody2D{});
    const engine::ecs::Entity crate = add_box(world, glm::vec3{2.2f, 1.85f, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    make_dynamic(world, crate, glm::vec3{0.f}, 1.f);

    step_physics(world);
    const glm::vec3 first = engine::ecs::world_position(world, crate);
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_LT(first.x, 2.2f);
    EXPECT_LT(first.y, 1.85f);
    EXPECT_NEAR(first.x, 2.193781f, 1e-4f);
    EXPECT_NEAR(first.y, 1.845772f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.x, -0.307692f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.y, -0.384615f, 1e-4f);
    EXPECT_NEAR(rigid.angular_velocity, 0.923077f, 1e-4f);
    EXPECT_NEAR(engine::angle_z(world.get<engine::Transform>(crate).rotation), 0.f, 1e-5f);

    step_physics(world);
    const glm::vec3 second = engine::ecs::world_position(world, crate);
    EXPECT_LT(second.x, 2.2f);
    EXPECT_LT(second.y, 1.85f);
    EXPECT_NEAR(second.x, 2.182021f, 1e-4f);
    EXPECT_NEAR(second.y, 1.835958f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.x, -0.605640f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.y, -0.788720f, 1e-4f);
    EXPECT_NEAR(rigid.angular_velocity, 1.858067f, 1e-4f);
    EXPECT_NEAR(engine::angle_z(world.get<engine::Transform>(crate).rotation), 0.015385f, 1e-4f);
}

TEST(Physics2D, PolygonRejectsConcaveAndShortRings) {
    engine::ecs::World clockwise = physics_world();
    const engine::ecs::Entity bad = clockwise.create();
    clockwise.emplace<engine::Transform>(bad, engine::Transform{});
    engine::PolygonCollider2D reversed{};
    reversed.count = 3;
    reversed.vertices[0] = {0.f, 0.f};
    reversed.vertices[1] = {0.f, 2.f};
    reversed.vertices[2] = {2.f, 0.f};
    clockwise.emplace<engine::PolygonCollider2D>(bad, reversed);
    add_box(clockwise, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(clockwise);
    EXPECT_TRUE(read_collisions(clockwise).empty());

    engine::ecs::World short_ring = physics_world();
    const engine::ecs::Entity edge = short_ring.create();
    short_ring.emplace<engine::Transform>(edge, engine::Transform{});
    engine::PolygonCollider2D segment{};
    segment.count = 2;
    segment.vertices[0] = {-1.f, 0.f};
    segment.vertices[1] = {1.f, 0.f};
    short_ring.emplace<engine::PolygonCollider2D>(edge, segment);
    add_box(short_ring, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    step_physics(short_ring);
    EXPECT_TRUE(read_collisions(short_ring).empty());
}

TEST(Physics2D, StaticPolygonContactsABox) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity slope = world.create();
    world.emplace<engine::Transform>(slope, engine::Transform{});
    engine::PolygonCollider2D polygon{};
    polygon.count = 3;
    polygon.vertices[0] = {0.f, 0.f};
    polygon.vertices[1] = {4.f, 0.f};
    polygon.vertices[2] = {4.f, 2.f};
    world.emplace<engine::PolygonCollider2D>(slope, polygon);
    // (2.2, 1.85) is tangent. This is the pose after one gravity step, still static.
    const engine::ecs::Entity box =
            add_box(world, glm::vec3{2.2f, 1.85f - engine::kFixed, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    world.emplace<engine::RigidBody2D>(box, engine::RigidBody2D{});

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    const float inv_root = 1.f / std::sqrt(5.f);
    EXPECT_NEAR(events[0].normal.x, -inv_root, 1e-4f);
    EXPECT_NEAR(events[0].normal.y, 2.f * inv_root, 1e-4f);
    EXPECT_NEAR(events[0].penetration, 0.014907f, 1e-4f);
}

TEST(Physics2D, PolygonInertiaMatchesTheRingFormula) {
    const glm::vec2 square[4] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
    EXPECT_NEAR(engine::inertia_2d_polygon(1.f, square, 4), 2.f / 3.f, 1e-6f);
    EXPECT_NEAR(engine::inertia_2d_polygon(1.f, square, 4), engine::inertia_2d_box(1.f, 2.f, 2.f), 1e-6f);

    const glm::vec2 right_triangle[3] = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}};
    EXPECT_NEAR(engine::inertia_2d_polygon(1.f, right_triangle, 3), 1.f / 3.f, 1e-6f);

    const glm::vec2 clockwise[3] = {{0.f, 0.f}, {0.f, 1.f}, {1.f, 0.f}};
    EXPECT_EQ(engine::inertia_2d_polygon(1.f, clockwise, 3), 0.f);
}


TEST(Physics2D, PolygonInertiaMatchesBox2DExactTriangleFanForRectangles) {
    struct Case {
        float mass;
        float width;
        float height;
        float angle;
    };
    const Case cases[] = {
            {1.f, 4.f, 2.f, 0.f},
            {2.5f, 6.f, 1.5f, 0.37f},
            {0.75f, 3.f, 5.f, 1.12f},
            {10.f, 8.f, 2.f, glm::quarter_pi<float>()},
    };
    for (const Case& c : cases) {
        const float hw = 0.5f * c.width;
        const float hh = 0.5f * c.height;
        const float cos_a = std::cos(c.angle);
        const float sin_a = std::sin(c.angle);
        const glm::vec2 local[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        glm::vec2 rotated[4]{};
        for (int i = 0; i < 4; ++i) {
            rotated[i] = {cos_a * local[i].x - sin_a * local[i].y, sin_a * local[i].x + cos_a * local[i].y};
        }
        const float expected = engine::inertia_2d_box(c.mass, c.width, c.height);
        const float got = engine::inertia_2d_polygon(c.mass, rotated, 4);
        EXPECT_NEAR(got, expected, 1e-5f * expected);
    }
}
