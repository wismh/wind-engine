#include <gtest/gtest.h>

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>

#include "ecs/contact_islands.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

[[nodiscard]] engine::ecs::World make_physics_world(glm::vec3 gravity = {0.f, -60.f, 0.f},
                                                    engine::JobSystem* jobs = nullptr) {
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.jobs = jobs});
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

// LQ-18: Static floor bodies do not bridge independent dynamic stacks into a single island.
TEST(Physics3DStability, ContactIslandsDoNotMergeAcrossStaticFloor) {
    // Body 0 is static floor; bodies 1..3 form stack A; bodies 4..6 form stack B.
    constexpr int kBodyCount = 7;
    std::vector<engine::ecs::Entity> body_entities(kBodyCount);
    for (int i = 0; i < kBodyCount; ++i) {
        body_entities[static_cast<std::size_t>(i)] = engine::ecs::Entity{static_cast<std::uint32_t>(i + 1), 1};
    }
    std::vector<std::uint8_t> dynamic_mask(kBodyCount, 1u);
    dynamic_mask[0] = 0u;

    const std::vector<engine::IslandBodyPair> pairs = {
            {0, 1}, // Stack A on floor
            {1, 2},
            {2, 3},
            {0, 4}, // Stack B on floor
            {4, 5},
            {5, 6},
    };
    const std::vector<std::uint8_t> solid_pair(pairs.size(), 1u);

    const std::vector<engine::ContactIsland> islands =
            engine::build_contact_islands(body_entities, pairs, solid_pair, dynamic_mask);

    ASSERT_EQ(islands.size(), 2u);
    EXPECT_EQ(islands[0].dynamic_body_count, 3);
    EXPECT_EQ(islands[0].pairs, (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(islands[1].dynamic_body_count, 3);
    EXPECT_EQ(islands[1].pairs, (std::vector<int>{3, 4, 5}));
}

// LQ-18: Macro-level parallel island solving produces bitwise identical results to serial execution.
TEST(Physics3DStability, HybridSolverParallelIslandsBitDeterministic) {
    auto build_and_run = [](std::uint32_t worker_count) {
        engine::JobSystem jobs(worker_count);
        engine::ecs::World world = make_physics_world({0.f, -60.f, 0.f}, &jobs);

        // Shared static floor
        (void)add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{60.f, 2.f, 60.f}, 0.5f);

        constexpr int kStacks = 6;
        constexpr int kStackHeight = 6;
        constexpr float kBoxSize = 1.f;

        std::vector<engine::ecs::Entity> boxes;
        for (int s = 0; s < kStacks; ++s) {
            const float x = (static_cast<float>(s) - 2.5f) * 5.f;
            for (int i = 0; i < kStackHeight; ++i) {
                const float y = (kBoxSize * 0.5f) + static_cast<float>(i) * kBoxSize;
                const engine::ecs::Entity box =
                        add_box(world, glm::vec3{x, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, kBoxSize}, 0.5f);
                make_dynamic(world, box, 1.f);
                boxes.push_back(box);
            }
        }

        step_physics_n(world, 120);

        struct BodySnapshot3D {
            glm::vec3 position{};
            glm::quat rotation{};
            glm::vec3 velocity{};
            glm::vec3 angular_velocity{};
            bool sleeping{};
        };

        std::vector<BodySnapshot3D> snapshots;
        snapshots.reserve(boxes.size());
        for (engine::ecs::Entity box : boxes) {
            const auto& transform = world.get<engine::Transform>(box);
            const auto& body = world.get<engine::RigidBody3D>(box);
            snapshots.push_back(BodySnapshot3D{
                    .position = transform.position,
                    .rotation = transform.rotation,
                    .velocity = body.velocity,
                    .angular_velocity = body.angular_velocity,
                    .sleeping = body.sleeping,
            });
        }
        return snapshots;
    };

    const auto serial = build_and_run(0);
    const auto parallel_4 = build_and_run(4);

    ASSERT_EQ(serial.size(), parallel_4.size());
    for (std::size_t i = 0; i < serial.size(); ++i) {
        EXPECT_EQ(serial[i].position, parallel_4[i].position) << "Mismatch at body " << i;
        EXPECT_EQ(serial[i].rotation, parallel_4[i].rotation) << "Mismatch at body " << i;
        EXPECT_EQ(serial[i].velocity, parallel_4[i].velocity) << "Mismatch at body " << i;
        EXPECT_EQ(serial[i].angular_velocity, parallel_4[i].angular_velocity) << "Mismatch at body " << i;
        EXPECT_EQ(serial[i].sleeping, parallel_4[i].sleeping) << "Mismatch at body " << i;
    }
}

// LQ-18: Micro-level graph coloring for large 3D islands (> 100 dynamic bodies) is bit-deterministic.
TEST(Physics3DStability, HybridSolverLargeIslandGraphColoringBitDeterministic) {
    auto build_and_run = [](std::uint32_t worker_count) {
        engine::JobSystem jobs(worker_count);
        engine::ecs::World world = make_physics_world({0.f, -60.f, 0.f}, &jobs);

        // Floor
        (void)add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{30.f, 2.f, 30.f}, 0.5f);

        // 5x5x5 = 125 slightly overlapping dynamic boxes so all 125 form a single connected island (> 100 bodies)
        constexpr int kGridX = 5;
        constexpr int kGridY = 5;
        constexpr int kGridZ = 5;
        constexpr float kBoxSize = 1.f;
        constexpr float kPitch = 0.98f;

        std::vector<engine::ecs::Entity> boxes;
        boxes.reserve(kGridX * kGridY * kGridZ);
        for (int y = 0; y < kGridY; ++y) {
            for (int z = 0; z < kGridZ; ++z) {
                for (int x = 0; x < kGridX; ++x) {
                    const float px = (static_cast<float>(x) - 2.f) * kPitch;
                    const float py = (kBoxSize * 0.5f) + static_cast<float>(y) * kPitch;
                    const float pz = (static_cast<float>(z) - 2.f) * kPitch;
                    const engine::ecs::Entity box =
                            add_box(world, glm::vec3{px, py, pz}, glm::vec3{kBoxSize, kBoxSize, kBoxSize}, 0.5f);
                    make_dynamic(world, box, 1.f);
                    boxes.push_back(box);
                }
            }
        }

        step_physics_n(world, 60);

        struct BodySnapshot3D {
            glm::vec3 position{};
            glm::quat rotation{};
            glm::vec3 velocity{};
            glm::vec3 angular_velocity{};
            bool sleeping{};
        };

        std::vector<BodySnapshot3D> snapshots;
        snapshots.reserve(boxes.size());
        for (engine::ecs::Entity box : boxes) {
            const auto& transform = world.get<engine::Transform>(box);
            const auto& body = world.get<engine::RigidBody3D>(box);
            snapshots.push_back(BodySnapshot3D{
                    .position = transform.position,
                    .rotation = transform.rotation,
                    .velocity = body.velocity,
                    .angular_velocity = body.angular_velocity,
                    .sleeping = body.sleeping,
            });
        }
        return snapshots;
    };

    const auto serial = build_and_run(0);
    const auto parallel_4 = build_and_run(4);

    ASSERT_EQ(serial.size(), 125u);
    ASSERT_EQ(serial.size(), parallel_4.size());
    for (std::size_t i = 0; i < serial.size(); ++i) {
        EXPECT_EQ(serial[i].position, parallel_4[i].position) << "Mismatch at body " << i;
        EXPECT_EQ(serial[i].rotation, parallel_4[i].rotation) << "Mismatch at body " << i;
        EXPECT_EQ(serial[i].velocity, parallel_4[i].velocity) << "Mismatch at body " << i;
        EXPECT_EQ(serial[i].angular_velocity, parallel_4[i].angular_velocity) << "Mismatch at body " << i;
        EXPECT_EQ(serial[i].sleeping, parallel_4[i].sleeping) << "Mismatch at body " << i;
    }
}
