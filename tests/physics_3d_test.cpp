#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world_serializer.h>

#include <glm/gtc/quaternion.hpp>

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

std::vector<engine::CollisionEvent3D> read_collisions(engine::ecs::World& world) {
    std::vector<engine::CollisionEvent3D> events;
    for (const engine::CollisionEvent3D& event : engine::ecs::EventReader<engine::CollisionEvent3D>{world}) {
        events.push_back(event);
    }
    return events;
}

std::vector<engine::CollisionEvent2D> read_collisions_2d(engine::ecs::World& world) {
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

void make_dynamic(engine::ecs::World& world, engine::ecs::Entity entity, glm::vec3 velocity) {
    world.emplace<engine::RigidBody3D>(entity, engine::RigidBody3D{
                                                       .velocity = velocity,
                                                       .type = engine::BodyType::Dynamic,
                                                       .mass = 1.f,
                                               });
}

engine::ecs::Entity add_sphere(engine::ecs::World& world, glm::vec3 position, float radius) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::SphereCollider3D>(entity, engine::SphereCollider3D{.radius = radius});
    return entity;
}

engine::ecs::Entity add_box(engine::ecs::World& world, glm::vec3 position, glm::vec3 size) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider3D>(entity, engine::BoxCollider3D{.size = size});
    return entity;
}

}

TEST(Physics3D, DynamicGravityOneStepWithoutFloor) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics3DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity entity = add_sphere(world, glm::vec3{0.f}, 0.5f);
    make_dynamic(world, entity, glm::vec3{0.f});

    step_physics(world);

    const engine::RigidBody3D& body = world.get<engine::RigidBody3D>(entity);
    EXPECT_NEAR(body.velocity.y, -1.f, 1e-5f);
    EXPECT_NEAR(body.velocity.x, 0.f, 1e-5f);
    EXPECT_NEAR(body.velocity.z, 0.f, 1e-5f);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.y, -engine::kFixed, 1e-5f);
}

TEST(Physics3D, SphereRestsOnStaticFloor) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics3DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 8.f});
    const engine::ecs::Entity sphere = add_sphere(world, glm::vec3{0.f, 2.f, 0.f}, 0.5f);
    make_dynamic(world, sphere, glm::vec3{0.f});

    step_physics_n(world, 30);
    const float rest_y = 0.5f - engine::kContactSlop;
    EXPECT_NEAR(engine::ecs::world_position(world, sphere).y, rest_y, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody3D>(sphere).velocity.y, 0.f, 1e-4f);

    step_physics_n(world, 90);
    EXPECT_NEAR(engine::ecs::world_position(world, sphere).y, rest_y, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody3D>(sphere).velocity.y, 0.f, 1e-4f);
    EXPECT_TRUE(world.get<engine::RigidBody3D>(sphere).sleeping);
    EXPECT_EQ(world.get<engine::Transform>(floor).position, (glm::vec3{0.f, -1.f, 0.f}));
}

TEST(Physics3D, BoxStackRestsWithNoTorque) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics3DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 8.f});
    const engine::ecs::Entity crate = add_box(world, glm::vec3{0.f, 2.f, 0.f}, glm::vec3{2.f, 2.f, 2.f});
    make_dynamic(world, crate, glm::vec3{0.f});

    step_physics_n(world, 30);
    const float rest_y = 1.f - engine::kContactSlop;
    EXPECT_NEAR(engine::ecs::world_position(world, crate).y, rest_y, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody3D>(crate).velocity.y, 0.f, 1e-4f);
    EXPECT_NEAR(glm::length(world.get<engine::RigidBody3D>(crate).angular_velocity), 0.f, 1e-5f);

    step_physics_n(world, 90);
    EXPECT_NEAR(engine::ecs::world_position(world, crate).y, rest_y, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody3D>(crate).velocity.y, 0.f, 1e-4f);
    EXPECT_NEAR(glm::length(world.get<engine::RigidBody3D>(crate).angular_velocity), 0.f, 1e-5f);
    EXPECT_TRUE(world.get<engine::RigidBody3D>(crate).sleeping);
    EXPECT_EQ(world.get<engine::Transform>(crate).rotation, (glm::quat{1.f, 0.f, 0.f, 0.f}));
    EXPECT_EQ(world.get<engine::Transform>(floor).position, (glm::vec3{0.f, -1.f, 0.f}));
}

TEST(Physics3D, RestitutionBounceAboveThreshold) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics3DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 8.f});
    world.get<engine::BoxCollider3D>(floor).restitution = 1.f;
    const engine::ecs::Entity sphere = add_sphere(world, glm::vec3{0.f, 0.5f, 0.f}, 0.5f);
    world.get<engine::SphereCollider3D>(sphere).restitution = 1.f;
    make_dynamic(world, sphere, glm::vec3{0.f, -3.f, 0.f});

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, sphere).y, 0.5f - engine::kContactSlop, 1e-4f);
    const engine::RigidBody3D& rigid = world.get<engine::RigidBody3D>(sphere);
    EXPECT_NEAR(rigid.velocity.y, 4.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.x, 0.f, 1e-4f);
}

TEST(Physics3D, FrictionRemovesOneUnitOnTheFirstRestingStep) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics3DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity floor = add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 8.f});
    world.get<engine::BoxCollider3D>(floor).friction = 1.f;
    const float rest_y = 0.5f - engine::kContactSlop;
    const engine::ecs::Entity sphere = add_sphere(world, glm::vec3{0.f, rest_y, 0.f}, 0.5f);
    world.get<engine::SphereCollider3D>(sphere).friction = 1.f;
    make_dynamic(world, sphere, glm::vec3{10.f, 0.f, 0.f});

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, sphere).y, rest_y, 1e-4f);
    const engine::RigidBody3D& rigid = world.get<engine::RigidBody3D>(sphere);
    EXPECT_NEAR(rigid.velocity.x, 9.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.y, 0.f, 1e-4f);
    EXPECT_NEAR(rigid.velocity.z, 0.f, 1e-4f);
    // Floor is created first, so the normal is +Y and the active tangent is -X.
    EXPECT_NEAR(rigid.angular_velocity.x, 0.f, 1e-4f);
    EXPECT_NEAR(rigid.angular_velocity.y, 0.f, 1e-4f);
    EXPECT_NEAR(rigid.angular_velocity.z, -4.9116665f, 1e-4f);
}

TEST(Physics3D, KinematicBoxPushesDynamicSphere) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity box = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 2.f});
    world.emplace<engine::RigidBody3D>(box, engine::RigidBody3D{.velocity = {60.f, 0.f, 0.f}});
    const engine::ecs::Entity sphere = add_sphere(world, glm::vec3{1.5f, 0.f, 0.f}, 0.5f);
    make_dynamic(world, sphere, glm::vec3{0.f});

    step_physics(world);

    EXPECT_NEAR(world.get<engine::Transform>(box).position.x, 1.f, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody3D>(box).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    EXPECT_NEAR(engine::ecs::world_position(world, sphere).x, 2.499f, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody3D>(sphere).velocity.x, 60.f, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody3D>(sphere).velocity.y, 0.f, 1e-4f);
}

TEST(Physics3D, RotatedParentKinematicPushesAlongWorldY) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity parent = world.create();
    world.emplace<engine::Transform>(parent, engine::Transform{.rotation = engine::rotation_z(glm::half_pi<float>())});
    const engine::ecs::Entity box = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 2.f});
    world.emplace<engine::RigidBody3D>(box, engine::RigidBody3D{.velocity = {60.f, 0.f, 0.f}});
    ASSERT_TRUE(engine::ecs::set_parent(world, box, parent));
    const engine::ecs::Entity sphere = add_sphere(world, glm::vec3{0.f, 1.5f, 0.f}, 0.5f);
    make_dynamic(world, sphere, glm::vec3{0.f});

    step_physics(world);

    EXPECT_NEAR(world.get<engine::Transform>(box).position.x, 1.f, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody3D>(box).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    EXPECT_NEAR(engine::ecs::world_position(world, box).y, 1.f, 1e-4f);
    EXPECT_NEAR(engine::ecs::world_position(world, sphere).y, 2.499f, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody3D>(sphere).velocity.y, 60.f, 1e-4f);
    EXPECT_NEAR(world.get<engine::RigidBody3D>(sphere).velocity.x, 0.f, 1e-4f);
}

TEST(Physics3D, DefaultBodyIsKinematicAndIgnoresGravityAndOverlap) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics3DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity wall = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 2.f});
    const engine::ecs::Entity mover = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 2.f});
    world.emplace<engine::RigidBody3D>(mover, engine::RigidBody3D{.velocity = {60.f, 0.f, 0.f}});

    step_physics(world);

    EXPECT_EQ(world.get<engine::RigidBody3D>(mover).type, engine::BodyType::Kinematic);
    EXPECT_NEAR(world.get<engine::Transform>(mover).position.x, 1.f, 1e-5f);
    EXPECT_NEAR(world.get<engine::Transform>(mover).position.y, 0.f, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody3D>(mover).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    EXPECT_EQ(world.get<engine::Transform>(wall).position, (glm::vec3{0.f}));
}

TEST(Physics3D, StaticOverlapEmitsEventAndMovesNeither) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity left = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 2.f});
    const engine::ecs::Entity right = add_box(world, glm::vec3{0.5f, 0.f, 0.f}, glm::vec3{2.f, 2.f, 2.f});
    world.emplace<engine::RigidBody3D>(left, engine::RigidBody3D{.type = engine::BodyType::Static});
    world.emplace<engine::RigidBody3D>(right, engine::RigidBody3D{.type = engine::BodyType::Static});

    step_physics(world);

    EXPECT_EQ(world.get<engine::Transform>(left).position, (glm::vec3{0.f}));
    EXPECT_EQ(world.get<engine::Transform>(right).position, (glm::vec3{0.5f, 0.f, 0.f}));
    const std::vector<engine::CollisionEvent3D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].phase, engine::CollisionPhase::Enter);
    EXPECT_FALSE(events[0].is_trigger);
    EXPECT_GT(events[0].penetration, 0.f);
}

TEST(Physics3D, TriggerDoesNotMoveDynamic) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity sphere = add_sphere(world, glm::vec3{0.f}, 0.5f);
    make_dynamic(world, sphere, glm::vec3{60.f, 0.f, 0.f});
    const engine::ecs::Entity trigger = add_box(world, glm::vec3{0.f}, glm::vec3{2.f, 2.f, 2.f});
    world.get<engine::BoxCollider3D>(trigger).is_trigger = true;

    step_physics(world);

    EXPECT_NEAR(engine::ecs::world_position(world, sphere).x, 1.f, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody3D>(sphere).velocity, (glm::vec3{60.f, 0.f, 0.f}));
    const std::vector<engine::CollisionEvent3D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].is_trigger);
}

TEST(Physics3D, ScaleDoesNotChangeSpherePenetration) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity left = add_sphere(world, glm::vec3{0.f}, 0.5f);
    world.get<engine::Transform>(left).scale = glm::vec3{4.f};
    add_sphere(world, glm::vec3{0.5f, 0.f, 0.f}, 0.5f);

    step_physics(world);

    const std::vector<engine::CollisionEvent3D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].penetration, 0.5f, 1e-5f);
}

TEST(Physics3D, NegativeSizeEmitsNoEvent) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity box = add_box(world, glm::vec3{0.f}, glm::vec3{-1.f, 1.f, 1.f});
    add_sphere(world, glm::vec3{0.f}, 0.5f);
    (void)box;

    step_physics(world);

    EXPECT_TRUE(read_collisions(world).empty());
}

TEST(Physics3D, FreeSpinKeepsAxisAndMatchesOmegaDt) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity entity = add_sphere(world, glm::vec3{0.f}, 0.5f);
    world.emplace<engine::RigidBody3D>(entity, engine::RigidBody3D{
                                                       .angular_velocity = {0.f, 2.f, 0.f},
                                                       .type = engine::BodyType::Dynamic,
                                               });

    step_physics(world);

    const glm::quat expected = glm::angleAxis(2.f * engine::kFixed, glm::vec3{0.f, 1.f, 0.f});
    const glm::quat actual = world.get<engine::Transform>(entity).rotation;
    EXPECT_NEAR(actual.w, expected.w, 1e-5f);
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody3D>(entity).angular_velocity, (glm::vec3{0.f, 2.f, 0.f}));
}

TEST(Physics3D, ChildSpinIsAppliedInWorldAxes) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity parent = world.create();
    world.emplace<engine::Transform>(parent, engine::Transform{.rotation = engine::rotation_z(glm::half_pi<float>())});
    const engine::ecs::Entity child = world.create();
    world.emplace<engine::Transform>(child, engine::Transform{});
    world.emplace<engine::RigidBody3D>(child, engine::RigidBody3D{
                                                      .angular_velocity = {0.f, 1.f, 0.f},
                                                      .type = engine::BodyType::Dynamic,
                                              });
    ASSERT_TRUE(engine::ecs::set_parent(world, child, parent));

    step_physics(world);

    const glm::quat delta = glm::angleAxis(engine::kFixed, glm::vec3{0.f, 1.f, 0.f});
    const glm::quat parent_rotation = engine::rotation_z(glm::half_pi<float>());
    const glm::quat expected = glm::normalize(glm::inverse(parent_rotation) * delta * parent_rotation);
    const glm::quat actual = world.get<engine::Transform>(child).rotation;
    EXPECT_NEAR(actual.w, expected.w, 1e-5f);
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

TEST(Physics3D, RigidBody3DSkipsSecondIntegrationFrom2D) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    world.ctx<engine::Physics3DConfig>().gravity = {0.f, -60.f, 0.f};
    const engine::ecs::Entity both = world.create();
    world.emplace<engine::Transform>(both, engine::Transform{});
    world.emplace<engine::RigidBody2D>(both, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    world.emplace<engine::RigidBody3D>(both, engine::RigidBody3D{.type = engine::BodyType::Dynamic});
    const engine::ecs::Entity only_2d = world.create();
    world.emplace<engine::Transform>(only_2d, engine::Transform{});
    world.emplace<engine::RigidBody2D>(only_2d, engine::RigidBody2D{.type = engine::BodyType::Dynamic});

    step_physics(world);

    EXPECT_NEAR(world.get<engine::RigidBody3D>(both).velocity.y, -1.f, 1e-5f);
    EXPECT_NEAR(world.get<engine::Transform>(both).position.y, -engine::kFixed, 1e-5f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(both).velocity.y, 0.f, 1e-5f);
    EXPECT_NEAR(world.get<engine::RigidBody2D>(only_2d).velocity.y, -1.f, 1e-5f);
    EXPECT_NEAR(world.get<engine::Transform>(only_2d).position.y, -engine::kFixed, 1e-5f);
}

TEST(Physics3D, RigidBody3DIsNotSolvedByThe2DPair) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity wall = world.create();
    world.emplace<engine::Transform>(wall, engine::Transform{});
    world.emplace<engine::BoxCollider2D>(wall, engine::BoxCollider2D{.size = {2.f, 2.f, 1.f}});
    world.emplace<engine::RigidBody3D>(wall, engine::RigidBody3D{});
    const engine::ecs::Entity crate = world.create();
    world.emplace<engine::Transform>(crate, engine::Transform{.position = {1.f, 0.f, 0.f}});
    world.emplace<engine::BoxCollider2D>(crate, engine::BoxCollider2D{.size = {2.f, 2.f, 1.f}});
    world.emplace<engine::RigidBody2D>(crate, engine::RigidBody2D{.type = engine::BodyType::Dynamic});

    step_physics(world);

    EXPECT_EQ(engine::ecs::world_position(world, crate), (glm::vec3{1.f, 0.f, 0.f}));
    EXPECT_TRUE(read_collisions_2d(world).empty());
}

TEST(Physics3D, SerializerRoundTripAndMissingTypeIsKinematic) {
    constexpr std::string_view kToml =
            "version = 1\n"
            "\n"
            "[[entities]]\n"
            "id = \"ball\"\n"
            "transform = { position = [0.0, 1.0, 0.0] }\n"
            "rigid_body_3d = { velocity = [1.0, 2.0, 3.0], angular_velocity = [4.0, 5.0, 6.0], "
            "type = \"dynamic\", mass = 3.5, gravity_scale = 0.5 }\n"
            "capsule_collider_3d = { radius = 0.25, height = 1.5, friction = 0.2, restitution = 0.4 }\n"
            "\n"
            "[[entities]]\n"
            "id = \"plain\"\n"
            "rigid_body_3d = { velocity = [8.0, 0.0, 0.0] }\n";

    engine::ecs::World world;
    auto loaded = engine::ecs::load_world_from_toml(world, kToml);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    const engine::RigidBody3D& body = world.get<engine::RigidBody3D>(loaded->by_id.at("ball"));
    EXPECT_EQ(body.type, engine::BodyType::Dynamic);
    EXPECT_FLOAT_EQ(body.mass, 3.5f);
    EXPECT_FLOAT_EQ(body.gravity_scale, 0.5f);
    EXPECT_EQ(body.velocity, (glm::vec3{1.f, 2.f, 3.f}));
    EXPECT_EQ(body.angular_velocity, (glm::vec3{4.f, 5.f, 6.f}));
    const engine::CapsuleCollider3D& capsule = world.get<engine::CapsuleCollider3D>(loaded->by_id.at("ball"));
    EXPECT_FLOAT_EQ(capsule.radius, 0.25f);
    EXPECT_FLOAT_EQ(capsule.height, 1.5f);
    EXPECT_FLOAT_EQ(capsule.friction, 0.2f);
    EXPECT_FLOAT_EQ(capsule.restitution, 0.4f);
    EXPECT_EQ(world.get<engine::RigidBody3D>(loaded->by_id.at("plain")).type, engine::BodyType::Kinematic);

    const std::string saved = engine::ecs::save_world_to_toml(world);
    engine::ecs::World again;
    auto reloaded = engine::ecs::load_world_from_toml(again, saved);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    const engine::RigidBody3D& round = again.get<engine::RigidBody3D>(reloaded->by_id.at("ball"));
    EXPECT_EQ(round.type, engine::BodyType::Dynamic);
    EXPECT_EQ(round.velocity, (glm::vec3{1.f, 2.f, 3.f}));
    EXPECT_EQ(round.angular_velocity, (glm::vec3{4.f, 5.f, 6.f}));
    EXPECT_FLOAT_EQ(round.mass, 3.5f);
    EXPECT_FLOAT_EQ(again.get<engine::CapsuleCollider3D>(reloaded->by_id.at("ball")).height, 1.5f);
    EXPECT_EQ(again.get<engine::RigidBody3D>(reloaded->by_id.at("plain")).type, engine::BodyType::Kinematic);
}

TEST(Physics3D, FirstShapePairKeepsItsManifold) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity body = world.create();
    world.emplace<engine::Transform>(body, engine::Transform{});
    world.emplace<engine::BoxCollider3D>(body, engine::BoxCollider3D{.size = {2.f, 2.f, 2.f}});
    world.emplace<engine::SphereCollider3D>(body, engine::SphereCollider3D{.radius = 1.5f});
    const engine::ecs::Entity other = add_box(world, glm::vec3{1.2f, 0.f, 0.f}, glm::vec3{1.f, 1.f, 1.f});

    step_physics(world);

    const std::vector<engine::CollisionEvent3D> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].a, body);
    EXPECT_EQ(events[0].b, other);
    // The sphere overlap is 0.8 deep. The box, gathered first, is the 0.3 face.
    EXPECT_NEAR(events[0].normal.x, 1.f, 1e-5f);
    EXPECT_NEAR(events[0].normal.y, 0.f, 1e-5f);
    EXPECT_NEAR(events[0].normal.z, 0.f, 1e-5f);
    EXPECT_NEAR(events[0].penetration, 0.3f, 1e-5f);
    EXPECT_NEAR(events[0].point.x, 0.85f, 1e-5f);
    EXPECT_NEAR(events[0].point.y, 0.f, 1e-5f);
    EXPECT_NEAR(events[0].point.z, 0.f, 1e-5f);
}
