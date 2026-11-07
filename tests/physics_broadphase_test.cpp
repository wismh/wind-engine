#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world_serializer.h>

#include "ecs/broadphase.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

std::vector<std::pair<int, int>> query(const std::vector<engine::Aabb>& boxes) {
    engine::Broadphase tree;
    tree.rebuild(boxes);
    std::vector<std::pair<int, int>> pairs;
    tree.query_pairs(pairs);
    return pairs;
}

std::vector<std::pair<int, int>> brute(const std::vector<engine::Aabb>& boxes) {
    std::vector<std::pair<int, int>> pairs;
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
        for (int j = i + 1; j < static_cast<int>(boxes.size()); ++j) {
            const engine::Aabb& a = boxes[static_cast<std::size_t>(i)];
            const engine::Aabb& b = boxes[static_cast<std::size_t>(j)];
            if (a.min.x > a.max.x || b.min.x > b.max.x) {
                continue;
            }
            if (engine::aabb_overlaps(a, b)) {
                pairs.emplace_back(i, j);
            }
        }
    }
    return pairs;
}

void step_physics(engine::ecs::World& world) {
    world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    world.run(engine::ecs::Schedule::Fixed);
}

void step_physics_n(engine::ecs::World& world, int steps) {
    for (int i = 0; i < steps; ++i) {
        step_physics(world);
    }
}

engine::ecs::World physics_world() {
    engine::ecs::World world;
    engine::register_engine_systems(world);
    return world;
}

engine::ecs::Entity add_box_2d(engine::ecs::World& world, glm::vec3 position, glm::vec3 size) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{.size = size});
    return entity;
}

engine::ecs::Entity add_sphere(engine::ecs::World& world, glm::vec3 position, float radius) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::SphereCollider3D>(entity, engine::SphereCollider3D{.radius = radius});
    return entity;
}

engine::ecs::Entity add_box_3d(engine::ecs::World& world, glm::vec3 position, glm::vec3 size) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider3D>(entity, engine::BoxCollider3D{.size = size});
    return entity;
}

int enter_count_2d(engine::ecs::World& world) {
    int count = 0;
    for (const engine::CollisionEvent2D& event : engine::ecs::EventReader<engine::CollisionEvent2D>{world}) {
        if (event.phase == engine::CollisionPhase::Enter) {
            ++count;
        }
    }
    return count;
}

int enter_count_3d(engine::ecs::World& world) {
    int count = 0;
    for (const engine::CollisionEvent3D& event : engine::ecs::EventReader<engine::CollisionEvent3D>{world}) {
        if (event.phase == engine::CollisionPhase::Enter) {
            ++count;
        }
    }
    return count;
}

}

TEST(Physics2D, BroadphaseClosedBounds) {
    const engine::Aabb left{.min = {0.f, 0.f, -1.f}, .max = {1.f, 1.f, 1.f}};
    const engine::Aabb touch{.min = {1.f, 0.f, -1.f}, .max = {2.f, 1.f, 1.f}};
    const float gap = std::nextafter(1.f, 2.f);
    const engine::Aabb apart{.min = {gap, 0.f, -1.f}, .max = {2.f, 1.f, 1.f}};
    EXPECT_EQ(query({left, touch}).size(), 1u);
    EXPECT_TRUE(query({left, apart}).empty());

    const engine::Aabb slab_a{.min = {0.f, 0.f, -1.f}, .max = {1.f, 1.f, 1.f}};
    const engine::Aabb slab_b{.min = {3.f, 0.f, -1.f}, .max = {4.f, 1.f, 1.f}};
    EXPECT_TRUE(query({slab_a, slab_b}).empty());

    const engine::Aabb z_gap{.min = {0.f, 0.f, 2.f}, .max = {1.f, 1.f, 3.f}};
    EXPECT_TRUE(query({left, z_gap}).empty());

    std::vector<engine::Aabb> boxes;
    for (int i = 0; i < 32; ++i) {
        const float x = static_cast<float>(i) * 3.f;
        boxes.push_back(engine::Aabb{.min = {x, 0.f, -1.f}, .max = {x + 1.f, 1.f, 1.f}});
    }
    boxes.push_back(engine::Aabb{.min = {200.f, 0.f, -1.f}, .max = {201.f, 1.f, 1.f}});
    boxes.push_back(engine::Aabb{.min = {200.5f, 0.f, -1.f}, .max = {201.5f, 1.f, 1.f}});
    const std::vector<std::pair<int, int>> once = query(boxes);
    ASSERT_EQ(once.size(), 1u);
    EXPECT_EQ(once[0], (std::pair<int, int>{32, 33}));
    EXPECT_EQ(query(boxes), once);

    std::vector<engine::Aabb> mixed = boxes;
    mixed.push_back(engine::Aabb{.min = {0.f, 0.f, -1.f}, .max = {0.5f, 0.5f, 1.f}});
    mixed.push_back(engine::Aabb{.min = {1.f, 0.f, 0.f}, .max = {-1.f, 1.f, 1.f}});
    auto got = query(mixed);
    auto expect = brute(mixed);
    std::sort(got.begin(), got.end());
    std::sort(expect.begin(), expect.end());
    EXPECT_EQ(got, expect);
}

TEST(Physics2D, BroadphaseOneEnterAmongSeparatedBoxes) {
    engine::ecs::World world = physics_world();
    for (int i = 0; i < 32; ++i) {
        add_box_2d(world, glm::vec3{static_cast<float>(i) * 5.f, 0.f, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    }
    add_box_2d(world, glm::vec3{1000.f, 0.f, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    const engine::ecs::Entity mover2d = add_box_2d(world, glm::vec3{1000.5f, 0.f, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    world.emplace<engine::RigidBody2D>(mover2d, engine::RigidBody2D{});

    step_physics(world);

    EXPECT_EQ(enter_count_2d(world), 1);
}

TEST(Physics2D, TenThousandStaticCollidersNeverGenerateStaticPairs) {
    engine::ecs::World world = physics_world();
    // 100 x 100 contiguous grid of static 1x1 boxes = 10,000 static colliders
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            add_box_2d(world, glm::vec3{static_cast<float>(x), static_cast<float>(y), 0.f}, glm::vec3{1.f, 1.f, 1.f});
        }
    }
    const engine::ecs::Entity mover = add_box_2d(world, glm::vec3{50.f, 100.5f, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    world.emplace<engine::RigidBody2D>(mover, engine::RigidBody2D{});

    step_physics(world);
    EXPECT_LE(enter_count_2d(world), 2);

    for (int i = 0; i < 4; ++i) {
        step_physics(world);
    }
}

TEST(Physics3D, BroadphaseOneEnterAmongSeparatedSpheres) {
    engine::ecs::World world = physics_world();
    for (int i = 0; i < 32; ++i) {
        add_sphere(world, glm::vec3{static_cast<float>(i) * 5.f, 0.f, 0.f}, 0.4f);
    }
    add_sphere(world, glm::vec3{1000.f, 0.f, 0.f}, 0.5f);
    const engine::ecs::Entity mover3d = add_sphere(world, glm::vec3{1000.5f, 0.f, 0.f}, 0.5f);
    world.emplace<engine::RigidBody3D>(mover3d, engine::RigidBody3D{});

    step_physics(world);

    EXPECT_EQ(enter_count_3d(world), 1);
}

TEST(Physics2D, SleepingCrateWakesWhenKinematicMoves) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};
    add_box_2d(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{8.f, 2.f, 1.f});
    const engine::ecs::Entity crate = add_box_2d(world, glm::vec3{0.f, 2.f, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.emplace<engine::RigidBody2D>(crate, engine::RigidBody2D{.type = engine::BodyType::Dynamic, .mass = 1.f});

    step_physics_n(world, 120);
    const float rest_y = 1.f - engine::kContactSlop;
    EXPECT_NEAR(engine::ecs::world_position(world, crate).y, rest_y, 1e-3f);
    EXPECT_TRUE(world.get<engine::RigidBody2D>(crate).sleeping);

    const engine::ecs::Entity platform = add_box_2d(world, glm::vec3{-2.f, rest_y, 0.f}, glm::vec3{2.f, 2.f, 1.f});
    world.emplace<engine::RigidBody2D>(platform, engine::RigidBody2D{.velocity = {60.f, 0.f, 0.f}});
    const float x_before = engine::ecs::world_position(world, crate).x;
    step_physics(world);

    EXPECT_GT(engine::ecs::world_position(world, crate).x, x_before);
    EXPECT_FALSE(world.get<engine::RigidBody2D>(crate).sleeping);
}

TEST(Physics2D, ZeroGravityHoverStaysAwake) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity crate = add_box_2d(world, glm::vec3{0.f}, glm::vec3{1.f, 1.f, 1.f});
    world.emplace<engine::RigidBody2D>(crate, engine::RigidBody2D{.type = engine::BodyType::Dynamic, .mass = 1.f});

    step_physics_n(world, 120);

    EXPECT_FALSE(world.get<engine::RigidBody2D>(crate).sleeping);
    EXPECT_EQ(world.get<engine::Transform>(crate).position, (glm::vec3{0.f}));
}

TEST(Physics2D, KinematicIgnoresSleepingFlag) {
    engine::ecs::World world = physics_world();
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{
                                                       .velocity = {60.f, 0.f, 0.f},
                                                       .sleeping = true,
                                               });

    step_physics(world);

    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 1.f, 1e-5f);
    EXPECT_EQ(world.get<engine::RigidBody2D>(entity).velocity, (glm::vec3{60.f, 0.f, 0.f}));
}

TEST(Physics3D, DistantSphereStacksSleepIndependently) {
    engine::ecs::World world = physics_world();
    world.ctx<engine::Physics3DConfig>().gravity = {0.f, -60.f, 0.f};
    add_box_3d(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{4.f, 2.f, 4.f});
    const engine::ecs::Entity left = add_sphere(world, glm::vec3{0.f, 2.f, 0.f}, 0.5f);
    world.emplace<engine::RigidBody3D>(left, engine::RigidBody3D{.type = engine::BodyType::Dynamic, .mass = 1.f});
    add_box_3d(world, glm::vec3{80.f, -1.f, 0.f}, glm::vec3{4.f, 2.f, 4.f});
    const engine::ecs::Entity right = add_sphere(world, glm::vec3{80.f, 2.f, 0.f}, 0.5f);
    world.emplace<engine::RigidBody3D>(right, engine::RigidBody3D{.type = engine::BodyType::Dynamic, .mass = 1.f});

    step_physics_n(world, 120);
    const float rest_y = 0.5f - engine::kContactSlop;
    EXPECT_NEAR(engine::ecs::world_position(world, left).y, rest_y, 1e-4f);
    EXPECT_NEAR(engine::ecs::world_position(world, right).y, rest_y, 1e-4f);
    EXPECT_TRUE(world.get<engine::RigidBody3D>(left).sleeping);
    EXPECT_TRUE(world.get<engine::RigidBody3D>(right).sleeping);

    world.get<engine::RigidBody3D>(left).velocity = {30.f, 0.f, 0.f};
    step_physics(world);

    EXPECT_GT(engine::ecs::world_position(world, left).x, 0.f);
    EXPECT_NEAR(engine::ecs::world_position(world, right).y, rest_y, 1e-4f);
    EXPECT_NEAR(engine::ecs::world_position(world, right).x, 80.f, 1e-4f);
    EXPECT_TRUE(world.get<engine::RigidBody3D>(right).sleeping);
}

TEST(WorldSerializer, RigidBodySleepingRoundTripAndMissingKey) {
    constexpr std::string_view kAsleep = R"(
version = 1

[[entities]]
id = "crate"
rigid_body_2d = { type = "dynamic", sleeping = true }

[[entities]]
id = "ball"
rigid_body_3d = { type = "dynamic", sleeping = true }
)";
    engine::ecs::World world;
    auto loaded = engine::ecs::load_world_from_toml(world, kAsleep);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    EXPECT_TRUE(world.get<engine::RigidBody2D>(loaded->by_id.at("crate")).sleeping);
    EXPECT_TRUE(world.get<engine::RigidBody3D>(loaded->by_id.at("ball")).sleeping);

    const std::string saved = engine::ecs::save_world_to_toml(world);
    engine::ecs::World again;
    auto reloaded = engine::ecs::load_world_from_toml(again, saved);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    EXPECT_TRUE(again.get<engine::RigidBody2D>(reloaded->by_id.at("crate")).sleeping);
    EXPECT_TRUE(again.get<engine::RigidBody3D>(reloaded->by_id.at("ball")).sleeping);

    constexpr std::string_view kOmitted = R"(
version = 1

[[entities]]
id = "crate"
rigid_body_2d = { type = "dynamic" }

[[entities]]
id = "ball"
rigid_body_3d = { type = "dynamic" }
)";
    engine::ecs::World omitted;
    auto missing = engine::ecs::load_world_from_toml(omitted, kOmitted);
    ASSERT_TRUE(missing.has_value()) << missing.error().message;
    EXPECT_FALSE(omitted.get<engine::RigidBody2D>(missing->by_id.at("crate")).sleeping);
    EXPECT_FALSE(omitted.get<engine::RigidBody3D>(missing->by_id.at("ball")).sleeping);
}
