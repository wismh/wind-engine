#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>

#include <algorithm>
#include <cmath>
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

[[nodiscard]] std::vector<engine::CollisionEvent3D> read_collisions(engine::ecs::World& world) {
    std::vector<engine::CollisionEvent3D> events;
    for (const engine::CollisionEvent3D& event : engine::ecs::EventReader<engine::CollisionEvent3D>{world}) {
        events.push_back(event);
    }
    return events;
}

[[nodiscard]] engine::ecs::World make_physics_world(glm::vec3 gravity = {0.f, -60.f, 0.f}) {
    engine::ecs::World world;
    engine::register_engine_systems(world);
    world.ctx<engine::Physics3DConfig>().gravity = gravity;
    return world;
}

[[nodiscard]] engine::ecs::Entity add_box(engine::ecs::World& world, glm::vec3 position, glm::vec3 size,
                                          float friction = 0.5f, float restitution = 0.f) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider3D>(entity, engine::BoxCollider3D{
                                                         .size = size,
                                                         .friction = friction,
                                                         .restitution = restitution,
                                                 });
    return entity;
}

void make_dynamic(engine::ecs::World& world, engine::ecs::Entity entity, float mass = 1.f,
                  glm::vec3 velocity = glm::vec3{0.f}) {
    world.emplace<engine::RigidBody3D>(entity, engine::RigidBody3D{
                                                       .velocity = velocity,
                                                       .type = engine::BodyType::Dynamic,
                                                       .mass = mass,
                                               });
}

}

// 3D vertical box stack: 5 boxes resting on a static floor under gravity.
// Tests whether vertical stacking maintains horizontal alignment and converges to sleep.
TEST(Physics3DStability, VerticalStack5Boxes) {
    engine::ecs::World world = make_physics_world();

    // Static floor at y = -1, size 10x2x10 -> surface at y = 0
    (void)add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{10.f, 2.f, 10.f}, 0.5f);

    constexpr int kBoxes = 5;
    constexpr float kBoxSize = 1.f;
    std::vector<engine::ecs::Entity> stack;

    for (int i = 0; i < kBoxes; ++i) {
        const float y = (kBoxSize * 0.5f) + static_cast<float>(i) * kBoxSize;
        const engine::ecs::Entity box =
                add_box(world, glm::vec3{0.f, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, kBoxSize}, 0.5f);
        make_dynamic(world, box, 1.f);
        stack.push_back(box);
    }

    step_physics_n(world, 180);

    for (int i = 0; i < kBoxes; ++i) {
        const glm::vec3 pos = engine::ecs::world_position(world, stack[static_cast<std::size_t>(i)]);
        EXPECT_NEAR(pos.x, 0.f, 0.2f);
        EXPECT_NEAR(pos.z, 0.f, 0.2f);
        const auto* body = world.try_get<engine::RigidBody3D>(stack[static_cast<std::size_t>(i)]);
        ASSERT_NE(body, nullptr);
        EXPECT_TRUE(body->sleeping);
    }
}

// 3D static voxel grid stress test: 10x10x3 = 300 adjacent static cubes.
// Verifies that static-static contacts produce 0 CollisionEvent3D events.
TEST(Physics3DStability, MassiveVoxelGridStaticVsStatic) {
    engine::ecs::World world = make_physics_world();

    constexpr int kDimX = 10;
    constexpr int kDimZ = 10;
    constexpr int kDimY = 3;
    constexpr float kBlockSize = 1.f;

    for (int y = 0; y < kDimY; ++y) {
        for (int z = 0; z < kDimZ; ++z) {
            for (int x = 0; x < kDimX; ++x) {
                (void)add_box(world,
                              glm::vec3{static_cast<float>(x) * kBlockSize, static_cast<float>(y) * kBlockSize,
                                        static_cast<float>(z) * kBlockSize},
                              glm::vec3{kBlockSize, kBlockSize, kBlockSize});
            }
        }
    }

    // Add 1 dynamic box floating above
    const engine::ecs::Entity mover = add_box(
            world, glm::vec3{5.f, static_cast<float>(kDimY) * kBlockSize + 2.f, 5.f}, glm::vec3{1.f, 1.f, 1.f});
    make_dynamic(world, mover, 1.f);

    step_physics(world);

    const std::vector<engine::CollisionEvent3D> events = read_collisions(world);
    EXPECT_EQ(events.size(), 0u);
}
