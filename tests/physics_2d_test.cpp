#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

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

}

TEST(Physics2D, IntegrateFixedDeltaTime) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{glm::vec3{60.f, 0.f, 0.f}});

    step_physics(world);

    const engine::Transform& transform = world.get<engine::Transform>(entity);
    EXPECT_NEAR(transform.position.x, 1.f, 1e-5f);
    EXPECT_NEAR(transform.position.y, 0.f, 1e-5f);
    EXPECT_NEAR(transform.position.z, 0.f, 1e-5f);
}

TEST(Physics2D, AabbOverlap) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(a, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});
    world.emplace<engine::Transform>(b, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(b, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, a);
    EXPECT_EQ(events[0].b, b);
}

TEST(Physics2D, CollisionPhaseEnterThenStay) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(a, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});
    world.emplace<engine::Transform>(b, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(b, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});

    step_physics(world);
    {
        const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].phase, engine::CollisionPhase::Enter);
    }

    drop_collision_history(world);
    step_physics(world);
    {
        const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].phase, engine::CollisionPhase::Stay);
    }
}

TEST(Physics2D, CollisionPhaseExitThenReenter) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(a, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});
    world.emplace<engine::Transform>(b, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(b, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});

    step_physics(world);
    ASSERT_EQ(read_collisions(world).size(), 1u);

    world.get<engine::Transform>(b).position.x = 100.f;
    drop_collision_history(world);
    step_physics(world);
    {
        const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].phase, engine::CollisionPhase::Exit);
    }

    drop_collision_history(world);
    step_physics(world);
    EXPECT_TRUE(read_collisions(world).empty());

    world.get<engine::Transform>(b).position.x = 0.f;
    drop_collision_history(world);
    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, a);
    EXPECT_EQ(events[0].b, b);
    EXPECT_EQ(events[0].phase, engine::CollisionPhase::Enter);
}

TEST(Physics2D, CircleOverlap) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::CircleCollider2D>(a, engine::CircleCollider2D{.radius = 1.f});
    world.emplace<engine::Transform>(b, engine::Transform{glm::vec3{1.5f, 0.f, 0.f}});
    world.emplace<engine::CircleCollider2D>(b, engine::CircleCollider2D{.radius = 1.f});

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, a);
    EXPECT_EQ(events[0].b, b);
}

TEST(Physics2D, CircleNoOverlapBeyondRadius) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::CircleCollider2D>(a, engine::CircleCollider2D{.radius = 1.f});
    world.emplace<engine::Transform>(b, engine::Transform{glm::vec3{3.f, 0.f, 0.f}});
    world.emplace<engine::CircleCollider2D>(b, engine::CircleCollider2D{.radius = 1.f});

    step_physics(world);

    EXPECT_TRUE(read_collisions(world).empty());
}

TEST(Physics2D, BoxCircleOverlapCrossShape) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(a, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});
    world.emplace<engine::Transform>(b, engine::Transform{glm::vec3{1.5f, 0.f, 0.f}});
    world.emplace<engine::CircleCollider2D>(b, engine::CircleCollider2D{.radius = 1.f});

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, a);
    EXPECT_EQ(events[0].b, b);
}

TEST(Physics2D, LayerMaskFiltersCollision) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(a,
            engine::BoxCollider2D{.size = glm::vec3{2.f, 2.f, 1.f}, .layer = 0b01, .mask = 0b01});
    world.emplace<engine::Transform>(b, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(b,
            engine::BoxCollider2D{.size = glm::vec3{2.f, 2.f, 1.f}, .layer = 0b10, .mask = 0b10});

    step_physics(world);

    EXPECT_TRUE(read_collisions(world).empty());
}

TEST(Physics2D, LayerMaskDefaultStillCollidesWithAll) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(a, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});
    world.emplace<engine::Transform>(b, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(b, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});

    step_physics(world);

    ASSERT_EQ(read_collisions(world).size(), 1u);
}

TEST(Physics2D, TriggerFlagReportedOnEvent) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(a,
            engine::BoxCollider2D{.size = glm::vec3{2.f, 2.f, 1.f}, .is_trigger = true});
    world.emplace<engine::Transform>(b, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(b, engine::BoxCollider2D{glm::vec3{2.f, 2.f, 1.f}});

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].is_trigger);
}
