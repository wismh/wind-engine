#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/physics.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include <vector>

namespace {

void step_physics(engine::ecs::World& world) {
    world.ctx<engine::Time>().fixedDeltaTime = engine::FIXED;
    world.Run(engine::ecs::Schedule::Fixed);
}

std::vector<engine::CollisionEvent> read_collisions(engine::ecs::World& world) {
    std::vector<engine::CollisionEvent> events;
    for (const engine::CollisionEvent& event : engine::ecs::EventReader<engine::CollisionEvent>{world}) {
        events.push_back(event);
    }
    return events;
}

void drop_collision_history(engine::ecs::World& world) {
    world.FlushEvents();
    world.FlushEvents();
}

}

TEST(Physics, IntegrateFixedDeltaTime) {
    engine::ecs::World world;
    engine::RegisterEngineSystems(world);

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::RigidBody>(entity, engine::RigidBody{glm::vec3{60.f, 0.f, 0.f}});

    step_physics(world);

    const engine::Transform& transform = world.get<engine::Transform>(entity);
    EXPECT_NEAR(transform.position.x, 1.f, 1e-5f);
    EXPECT_NEAR(transform.position.y, 0.f, 1e-5f);
    EXPECT_NEAR(transform.position.z, 0.f, 1e-5f);
}

TEST(Physics, AabbOverlap) {
    engine::ecs::World world;
    engine::RegisterEngineSystems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider>(a, engine::BoxCollider{glm::vec3{2.f, 2.f, 1.f}});
    world.emplace<engine::Transform>(b, engine::Transform{});
    world.emplace<engine::BoxCollider>(b, engine::BoxCollider{glm::vec3{2.f, 2.f, 1.f}});

    step_physics(world);

    const std::vector<engine::CollisionEvent> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, a);
    EXPECT_EQ(events[0].b, b);
}

TEST(Physics, CollisionEventOnEnterNotStay) {
    engine::ecs::World world;
    engine::RegisterEngineSystems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider>(a, engine::BoxCollider{glm::vec3{2.f, 2.f, 1.f}});
    world.emplace<engine::Transform>(b, engine::Transform{});
    world.emplace<engine::BoxCollider>(b, engine::BoxCollider{glm::vec3{2.f, 2.f, 1.f}});

    step_physics(world);
    ASSERT_EQ(read_collisions(world).size(), 1u);

    drop_collision_history(world);
    step_physics(world);
    EXPECT_TRUE(read_collisions(world).empty());
}

TEST(Physics, CollisionEventReenterAfterSeparate) {
    engine::ecs::World world;
    engine::RegisterEngineSystems(world);

    const engine::ecs::Entity a = world.create();
    const engine::ecs::Entity b = world.create();
    world.emplace<engine::Transform>(a, engine::Transform{});
    world.emplace<engine::BoxCollider>(a, engine::BoxCollider{glm::vec3{2.f, 2.f, 1.f}});
    world.emplace<engine::Transform>(b, engine::Transform{});
    world.emplace<engine::BoxCollider>(b, engine::BoxCollider{glm::vec3{2.f, 2.f, 1.f}});

    step_physics(world);
    ASSERT_EQ(read_collisions(world).size(), 1u);

    world.get<engine::Transform>(b).position.x = 100.f;
    drop_collision_history(world);
    step_physics(world);
    EXPECT_TRUE(read_collisions(world).empty());

    world.get<engine::Transform>(b).position.x = 0.f;
    drop_collision_history(world);
    step_physics(world);

    const std::vector<engine::CollisionEvent> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, a);
    EXPECT_EQ(events[0].b, b);
}
