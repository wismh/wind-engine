#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>

#include <glm/gtc/constants.hpp>

#include <vector>

namespace {

void step_physics(engine::ecs::World& world) {
    world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    world.run(engine::ecs::Schedule::Fixed);
}

void step_physics_n(engine::ecs::World& world, int steps) {
    for (int i = 0; i < steps; ++i) {
        step_physics(world);
    }
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

engine::ecs::Entity add_box(engine::ecs::World& world, glm::vec3 position, glm::vec3 size) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = size});
    return entity;
}

void make_dynamic(engine::ecs::World& world, engine::ecs::Entity entity, glm::vec3 velocity, float mass) {
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{
            .velocity = velocity,
            .type = engine::BodyType::Dynamic,
            .mass = mass,
    });
}

}

TEST(Physics2D, StaticBodyDoesNotIntegrate) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{
            .velocity = {60.f, 0.f, 0.f},
            .type = engine::BodyType::Static,
    });

    step_physics(world);

    EXPECT_EQ(world.get<engine::Transform>(entity).position, (glm::vec3{0.f}));
    EXPECT_EQ(world.get<engine::RigidBody2D>(entity).velocity, (glm::vec3{60.f, 0.f, 0.f}));
}

TEST(Physics2D, DynamicGravityOneStepWithoutFloor) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    make_dynamic(world, entity, glm::vec3{0.f}, 1.f);

    step_physics(world);

    const engine::RigidBody2D& body = world.get<engine::RigidBody2D>(entity);
    EXPECT_NEAR(body.velocity.y, -1.f, 1e-5f);
    EXPECT_NEAR(body.velocity.x, 0.f, 1e-5f);
    EXPECT_NEAR(body.velocity.z, 0.f, 1e-5f);
    const glm::vec3 position = world.get<engine::Transform>(entity).position;
    EXPECT_NEAR(position.y, -engine::kFixed, 1e-5f);
    EXPECT_NEAR(position.x, 0.f, 1e-5f);
    EXPECT_NEAR(position.z, 0.f, 1e-5f);
}

TEST(Physics2D, DynamicBoxRestsOnStaticFloor) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 1.f});
    const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, 2.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, crate, glm::vec3{0.f}, 1.f);

    step_physics_n(world, 30);
    const float rest_y = 1.f - engine::kContactSlop;
    EXPECT_NEAR(engine::ecs::world_position(world, crate).y, rest_y, 1e-3f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(crate).velocity.y, 0.f, 1e-3f);

    step_physics_n(world, 90);
    const glm::vec3 crate_pos = engine::ecs::world_position(world, crate);
    EXPECT_NEAR(crate_pos.y, rest_y, 1e-3f);
    EXPECT_GT(crate_pos.y - 1.f, -engine::kContactSlop - 1e-3f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(crate).velocity.y, 0.f, 1e-3f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(crate).angular_velocity, 0.f, 1e-5f);
    EXPECT_TRUE(world.get<engine::RigidBody2D>(crate).sleeping);
    EXPECT_EQ(world.get<engine::Transform>(floor).position, (glm::vec3{0.f, -1.f, 0.f}));
}

TEST(Physics2D, DynamicLosesInwardVelocityAgainstStaticWall) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity wall = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 8.f, 1.f});
    const engine::ecs::Entity body = add_box(world, glm::vec3{-1.5f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, body, glm::vec3{60.f, 60.f, 0.f}, 1.f);

    step_physics(world);

    const glm::vec3 position = engine::ecs::world_position(world, body);
    EXPECT_NEAR(position.x, -1.999f, 1e-4f);
    EXPECT_NEAR(position.y, 1.f, 1e-4f);
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(body);
    EXPECT_NEAR(rigid.velocity.x, 0.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.y, 60.f, 1e-4f);
    EXPECT_EQ(world.get<engine::Transform>(wall).position, (glm::vec3{0.f}));
}

TEST(Physics2D, KinematicPlatformPushesDynamic) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity platform = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.emplace<engine::RigidBody2D>(platform, engine::RigidBody2D{glm::vec3{60.f, 0.f, 0.f}});
    const engine::ecs::Entity crate = add_box(world, glm::vec3{2.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, crate, glm::vec3{0.f}, 1.f);

    step_physics(world);

    EXPECT_NEAR(world.get<engine::Transform>(platform).position.x, 1.f, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody2D>(platform).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    EXPECT_NEAR(engine::ecs::world_position(world, crate).x, 2.999f, 1e-4f);
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_NEAR(rigid.velocity.x, 60.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.y, 0.f, 1e-4f);
}

TEST(Physics2D, TranslatedParentKinematicPushesDynamic) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity parent = world.create();
    world.emplace<engine::Transform>(parent, engine::Transform{.position = {5.f, 0.f, 0.f}});
    const engine::ecs::Entity platform = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.emplace<engine::RigidBody2D>(platform, engine::RigidBody2D{glm::vec3{60.f, 0.f, 0.f}});
    ASSERT_TRUE(engine::ecs::set_parent(world, platform, parent));
    const engine::ecs::Entity crate = add_box(world, glm::vec3{7.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, crate, glm::vec3{0.f}, 1.f);

    step_physics(world);

    EXPECT_EQ(world.get<engine::Transform>(parent).position, (glm::vec3{5.f, 0.f, 0.f}));
    EXPECT_NEAR(world.get<engine::Transform>(platform).position.x, 1.f, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody2D>(platform).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    EXPECT_NEAR(engine::ecs::world_position(world, platform).x, 6.f, 1e-4f);
    EXPECT_NEAR(engine::ecs::world_position(world, crate).x, 7.999f, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(crate).velocity.x, 60.f, 1e-4f);
}

TEST(Physics2D, ScaledParentKinematicPushesAtWorldRate) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity parent = world.create();
    world.emplace<engine::Transform>(parent, engine::Transform{.scale = {2.f, 1.f, 1.f}});
    const engine::ecs::Entity platform = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.emplace<engine::RigidBody2D>(platform, engine::RigidBody2D{glm::vec3{60.f, 0.f, 0.f}});
    ASSERT_TRUE(engine::ecs::set_parent(world, platform, parent));
    const engine::ecs::Entity crate = add_box(world, glm::vec3{3.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, crate, glm::vec3{0.f}, 1.f);

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, platform).x, 2.f, 1e-4f);
    EXPECT_NEAR(engine::ecs::world_position(world, platform).y, 0.f, 1e-4f);
    EXPECT_EQ(world.get<engine::RigidBody2D>(platform).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    EXPECT_NEAR(engine::ecs::world_position(world, crate).x, 3.999f, 1e-4f);
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_NEAR(rigid.velocity.x, 120.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.y, 0.f, 1e-5f);
}

TEST(Physics2D, RotatedParentKinematicPushesAlongWorldVelocity) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity parent = world.create();
    world.emplace<engine::Transform>(parent, engine::Transform{.rotation = engine::rotation_z(glm::half_pi<float>())});
    const engine::ecs::Entity platform = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.emplace<engine::RigidBody2D>(platform, engine::RigidBody2D{glm::vec3{60.f, 0.f, 0.f}});
    ASSERT_TRUE(engine::ecs::set_parent(world, platform, parent));
    const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, 2.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, crate, glm::vec3{0.f}, 1.f);

    step_physics(world);

    const glm::vec3 platform_world = engine::ecs::world_position(world, platform);
    EXPECT_NEAR(platform_world.x, 0.f, 1e-4f);
    EXPECT_NEAR(platform_world.y, 1.f, 1e-4f);
    EXPECT_EQ(world.get<engine::RigidBody2D>(platform).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    EXPECT_NEAR(engine::ecs::world_position(world, crate).y, 2.999f, 1e-4f);
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_NEAR(rigid.velocity.x, 0.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.y, 60.f, 1e-4f);
}

TEST(Physics2D, EqualMassDynamicsSeparateSymmetrically) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity left = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    const engine::ecs::Entity right = add_box(world, glm::vec3{1.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, left, glm::vec3{0.f}, 1.f);
    make_dynamic(world, right, glm::vec3{0.f}, 1.f);

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, left).x, -0.4995f, 1e-4f);
    EXPECT_NEAR(engine::ecs::world_position(world, right).x, 1.4995f, 1e-4f);
    EXPECT_EQ(world.get<engine::RigidBody2D>(left).velocity, (glm::vec3{0.f}));
    EXPECT_EQ(world.get<engine::RigidBody2D>(right).velocity, (glm::vec3{0.f}));
    const std::vector<engine::CollisionEvent2D> entered = read_collisions(world);
    ASSERT_EQ(entered.size(), 1u);
    EXPECT_EQ(entered[0].phase, engine::CollisionPhase::Enter);

    drop_collision_history(world);
    step_physics(world);
    const std::vector<engine::CollisionEvent2D> stayed = read_collisions(world);
    ASSERT_EQ(stayed.size(), 1u);
    EXPECT_EQ(stayed[0].phase, engine::CollisionPhase::Stay);
    EXPECT_NEAR(engine::ecs::world_position(world, left).x, -0.4995f, 1e-4f);
    EXPECT_NEAR(engine::ecs::world_position(world, right).x, 1.4995f, 1e-4f);
}

TEST(Physics2D, UnequalMassSeparatesByInverseMass) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity light = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    const engine::ecs::Entity heavy = add_box(world, glm::vec3{1.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, light, glm::vec3{0.f}, 1.f);
    make_dynamic(world, heavy, glm::vec3{0.f}, 3.f);

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, light).x, -0.74925f, 1e-4f);
    EXPECT_NEAR(engine::ecs::world_position(world, heavy).x, 1.24975f, 1e-4f);
}

TEST(Physics2D, TriggerDoesNotMoveDynamic) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity mover = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, mover, glm::vec3{60.f, 0.f, 0.f}, 1.f);
    const engine::ecs::Entity trigger = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(trigger).is_trigger = true;

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, mover).x, 1.f, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody2D>(mover).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    EXPECT_EQ(world.get<engine::Transform>(trigger).position, (glm::vec3{0.f}));
    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].is_trigger);
}

TEST(Physics2D, LayerMismatchDoesNotSolve) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity left = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    const engine::ecs::Entity right = add_box(world, glm::vec3{1.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(left).layer = 0b01;
    world.get<engine::BoxCollider2D>(left).mask = 0b01;
    world.get<engine::BoxCollider2D>(right).layer = 0b10;
    world.get<engine::BoxCollider2D>(right).mask = 0b10;
    make_dynamic(world, left, glm::vec3{0.f}, 1.f);
    make_dynamic(world, right, glm::vec3{0.f}, 1.f);

    step_physics(world);

    EXPECT_EQ(engine::ecs::world_position(world, left), (glm::vec3{0.f}));
    EXPECT_EQ(engine::ecs::world_position(world, right), (glm::vec3{1.f, 0.f, 0.f}));
    EXPECT_TRUE(read_collisions(world).empty());
}

TEST(Physics2D, DynamicChildUnderTranslatedParentSeparatesInWorld) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity parent = world.create();
    world.emplace<engine::Transform>(parent, engine::Transform{.position = {10.f, 0.f, 0.f}});
    const engine::ecs::Entity child = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, child, glm::vec3{0.f}, 1.f);
    ASSERT_TRUE(engine::ecs::set_parent(world, child, parent));
    const engine::ecs::Entity wall = add_box(world, glm::vec3{11.f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});

    step_physics(world);

    EXPECT_EQ(world.get<engine::Transform>(parent).position, (glm::vec3{10.f, 0.f, 0.f}));
    EXPECT_EQ(world.get<engine::Transform>(wall).position, (glm::vec3{11.f, 0.f, 0.f}));
    EXPECT_NEAR(engine::ecs::world_position(world, child).x, 9.001f, 1e-4f);
    EXPECT_NEAR(world.get<engine::Transform>(child).position.x, -0.999f, 1e-4f);
}

TEST(Physics2D, RestitutionBounceAboveThreshold) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(floor).restitution = 1.f;
    const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, 1.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(crate).restitution = 1.f;
    make_dynamic(world, crate, glm::vec3{0.f, -3.f, 0.f}, 1.f);

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, crate).y, 1.f - engine::kContactSlop, 1e-4f);
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_NEAR(rigid.velocity.y, 4.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.x, 0.f, 1e-4f);
}

TEST(Physics2D, RestitutionZeroDoesNotReverse) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 1.f});
    const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, 1.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, crate, glm::vec3{0.f, -3.f, 0.f}, 1.f);

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, crate).y, 1.f - engine::kContactSlop, 1e-4f);
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_NEAR(rigid.velocity.y, 0.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.x, 0.f, 1e-4f);
}

TEST(Physics2D, FrictionRemovesOneUnitPerRestingStep) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(floor).friction = 1.f;
    const float rest_y = 1.f - engine::kContactSlop;
    const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, rest_y, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(crate).friction = 1.f;
    make_dynamic(world, crate, glm::vec3{10.f, 0.f, 0.f}, 1.f);

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, crate).y, rest_y, 1e-4f);
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_NEAR(rigid.velocity.x, 9.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.y, 0.f, 1e-4f);
    EXPECT_NEAR(rigid.angular_velocity, -1.4867501f, 1e-4f);
    EXPECT_NEAR(engine::angle_z(world.get<engine::Transform>(crate).rotation), 0.f, 1e-6f);
}

TEST(Physics2D, FrictionZeroKeepsHorizontalSpeed) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 1.f});
    const float rest_y = 1.f - engine::kContactSlop;
    const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, rest_y, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    make_dynamic(world, crate, glm::vec3{10.f, 0.f, 0.f}, 1.f);

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, crate).y, rest_y, 1e-4f);
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_NEAR(rigid.velocity.x, 10.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.y, 0.f, 1e-4f);
    EXPECT_NEAR(rigid.angular_velocity, 0.f, 1e-5f);
}

TEST(Physics2D, FrictionRemovesAboutOneUnitOnLaterSteps) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{40.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(floor).friction = 1.f;
    // Wide and flat so the slide stays on the face. A square yaws, a corner digs in, and the
    // contact stops being a resting one.
    const float rest_y = 1.f - engine::kContactSlop;
    const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, rest_y, 0.f}, glm::vec3{16.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(crate).friction = 1.f;
    make_dynamic(world, crate, glm::vec3{10.f, 0.f, 0.f}, 1.f);

    // The cached tangent is applied as a delta, so each later step still removes about one unit.
    float previous = 10.f;
    for (int step = 0; step < 4; ++step) {
        step_physics(world);
        const float vx = world.get<engine::RigidBody2D>(crate).velocity.x;
        EXPECT_NEAR(previous - vx, 1.f, 0.15f);
        previous = vx;
    }
    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_NEAR(rigid.velocity.y, 0.f, 0.25f);
    EXPECT_FALSE(rigid.sleeping);
}

TEST(Physics2D, FirstSolidColliderSuppliesInertia) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(floor).friction = 1.f;
    const float rest_y = 1.f - engine::kContactSlop;
    const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, rest_y, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.get<engine::BoxCollider2D>(crate).friction = 1.f;
    // Inside the box, so it misses the floor. A circle of this radius would dominate I if it were counted.
    world.emplace<engine::CircleCollider2D>(crate, engine::CircleCollider2D{.radius = 0.5f, .friction = 1.f});
    make_dynamic(world, crate, glm::vec3{10.f, 0.f, 0.f}, 1.f);

    step_physics(world);

    const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
    EXPECT_NEAR(rigid.velocity.x, 9.f, 1e-4f);
    EXPECT_NEAR(rigid.angular_velocity, -1.4867501f, 1e-4f);
    ASSERT_EQ(read_collisions(world).size(), 1u);
}


TEST(Physics2D, RestitutionBelowBounceThresholdIsClampedToZero) {
    // Gravity is 0 so approach speed is controlled purely by initial velocity.
    // At normal_speed == -kBounceThreshold (-1.0f), restitution is clamped to 0 (no bounce).
    {
        engine::ecs::World world = physics_world();
        const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 1.f});
        world.get<engine::BoxCollider2D>(floor).restitution = 1.f;
        const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, 1.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
        world.get<engine::BoxCollider2D>(crate).restitution = 1.f;
        make_dynamic(world, crate, glm::vec3{0.f, -engine::kBounceThreshold, 0.f}, 1.f);

        step_physics(world);

        const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
        EXPECT_NEAR(rigid.velocity.y, 0.f, 1e-5f);
    }
    // Just above the threshold (-1.5f < -kBounceThreshold), full restitution bounce occurs.
    {
        engine::ecs::World world = physics_world();
        const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 1.f});
        world.get<engine::BoxCollider2D>(floor).restitution = 0.8f;
        const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, 1.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
        world.get<engine::BoxCollider2D>(crate).restitution = 0.8f;
        make_dynamic(world, crate, glm::vec3{0.f, -1.5f, 0.f}, 1.f);

        step_physics(world);

        const engine::RigidBody2D& rigid = world.get<engine::RigidBody2D>(crate);
        EXPECT_NEAR(rigid.velocity.y, 1.5f * 0.8f, 1e-4f);
    }
}
