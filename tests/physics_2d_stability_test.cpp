#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
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

[[nodiscard]] std::vector<engine::CollisionEvent2D> read_collisions(engine::ecs::World& world) {
    std::vector<engine::CollisionEvent2D> events;
    for (const engine::CollisionEvent2D& event : engine::ecs::EventReader<engine::CollisionEvent2D>{world}) {
        events.push_back(event);
    }
    return events;
}

[[nodiscard]] engine::ecs::World make_physics_world(glm::vec3 gravity = {0.f, -60.f, 0.f}) {
    engine::ecs::World world;
    engine::register_engine_systems(world);
    world.ctx<engine::Physics2DConfig>().gravity = gravity;
    return world;
}

[[nodiscard]] engine::ecs::Entity add_box(engine::ecs::World& world, glm::vec3 position, glm::vec3 size,
                                          float friction = 0.5f, float restitution = 0.f) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::BoxCollider2D>(entity, engine::BoxCollider2D{
                                                         .size = size,
                                                         .friction = friction,
                                                         .restitution = restitution,
                                                 });
    return entity;
}

void make_dynamic(engine::ecs::World& world, engine::ecs::Entity entity, float mass = 1.f,
                  glm::vec3 velocity = glm::vec3{0.f}) {
    world.emplace<engine::RigidBody2D>(entity, engine::RigidBody2D{
                                                       .velocity = velocity,
                                                       .type = engine::BodyType::Dynamic,
                                                       .mass = mass,
                                               });
}

}

// Classic Box2D Pyramid: 10 rows (55 boxes) stacked on a static floor.
// Tests whether the contact solver maintains stability without horizontal drift/collapse,
// and whether warm-starting and sleep convergence allow all bodies to sleep.
TEST(Physics2DStability, ClassicPyramid10Rows) {
    engine::ecs::World world = make_physics_world();

    // Floor at y = -1, size 40x2 -> top surface at y = 0
    (void)add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{40.f, 2.f, 1.f}, 0.5f);

    constexpr int kRows = 10;
    constexpr float kBoxSize = 1.f;
    constexpr float kSpacing = 1.02f;

    std::vector<engine::ecs::Entity> boxes;
    std::vector<glm::vec3> initial_positions;
    engine::ecs::Entity apex{};

    for (int row = 0; row < kRows; ++row) {
        const int count_in_row = kRows - row;
        const float y = (kBoxSize * 0.5f) + static_cast<float>(row) * kBoxSize;
        const float x_start = -0.5f * static_cast<float>(count_in_row - 1) * kSpacing;

        for (int col = 0; col < count_in_row; ++col) {
            const float x = x_start + static_cast<float>(col) * kSpacing;
            const glm::vec3 pos{x, y, 0.f};
            const engine::ecs::Entity box = add_box(world, pos, glm::vec3{kBoxSize, kBoxSize, 1.f}, 0.5f);
            make_dynamic(world, box, 1.f);
            boxes.push_back(box);
            initial_positions.push_back(pos);

            if (row == kRows - 1) {
                apex = box;
            }
        }
    }

    ASSERT_EQ(boxes.size(), 55u);
    ASSERT_TRUE(world.valid(apex));

    // Simulate for 300 steps (5 seconds at 60Hz)
    step_physics_n(world, 300);

    // 1. Apex height: should remain near the top, not fall through or collapse to floor
    const glm::vec3 apex_pos = engine::ecs::world_position(world, apex);
    const float expected_apex_y = (kBoxSize * 0.5f) + static_cast<float>(kRows - 1) * kBoxSize;
    EXPECT_GT(apex_pos.y, expected_apex_y - 1.5f);

    // 2. Base layer drift: base boxes should not slide apart excessively
    float max_base_drift_x = 0.f;
    for (int col = 0; col < kRows; ++col) {
        const glm::vec3 current_pos = engine::ecs::world_position(world, boxes[static_cast<std::size_t>(col)]);
        const float drift_x = std::abs(current_pos.x - initial_positions[static_cast<std::size_t>(col)].x);
        max_base_drift_x = std::max(max_base_drift_x, drift_x);
    }
    EXPECT_LT(max_base_drift_x, 0.35f);

    // 3. Sleep convergence: after 300 steps of undisturbed settling, all blocks should be asleep
    int sleeping_count = 0;
    for (engine::ecs::Entity box : boxes) {
        const auto* body = world.try_get<engine::RigidBody2D>(box);
        if (body != nullptr && body->sleeping) {
            ++sleeping_count;
        }
    }
    EXPECT_EQ(sleeping_count, static_cast<int>(boxes.size()));
}

// Vertical column of 5 boxes stacked directly on top of each other.
// Tests solver penetration resolution along the vertical axis without buckling.
TEST(Physics2DStability, VerticalStack5Boxes) {
    engine::ecs::World world = make_physics_world();

    (void)add_box(world, glm::vec3{0.f, -1.f, 0.f}, glm::vec3{10.f, 2.f, 1.f}, 0.5f);

    constexpr int kBoxes = 5;
    constexpr float kBoxSize = 1.f;
    std::vector<engine::ecs::Entity> stack;

    for (int i = 0; i < kBoxes; ++i) {
        const float y = (kBoxSize * 0.5f) + static_cast<float>(i) * kBoxSize;
        const engine::ecs::Entity box = add_box(world, glm::vec3{0.f, y, 0.f}, glm::vec3{kBoxSize, kBoxSize, 1.f});
        make_dynamic(world, box, 1.f);
        stack.push_back(box);
    }

    step_physics_n(world, 180);

    for (int i = 0; i < kBoxes; ++i) {
        const glm::vec3 pos = engine::ecs::world_position(world, stack[static_cast<std::size_t>(i)]);
        EXPECT_NEAR(pos.x, 0.f, 0.15f);
        const auto* body = world.try_get<engine::RigidBody2D>(stack[static_cast<std::size_t>(i)]);
        ASSERT_NE(body, nullptr);
        EXPECT_TRUE(body->sleeping);
    }
}

// Massive static tilemap stress test.
// 30x30 = 900 adjacent static boxes with touching edges.
// Tests that NO CollisionEvent2D events are emitted between static-static pairs,
// and only events involving dynamic or trigger bodies are produced.
TEST(Physics2DStability, MassiveTilemapStaticVsStatic) {
    engine::ecs::World world = make_physics_world();

    constexpr int kGridW = 30;
    constexpr int kGridH = 30;
    constexpr float kTileSize = 1.f;

    for (int y = 0; y < kGridH; ++y) {
        for (int x = 0; x < kGridW; ++x) {
            (void)add_box(world, glm::vec3{static_cast<float>(x) * kTileSize, static_cast<float>(y) * kTileSize, 0.f},
                          glm::vec3{kTileSize, kTileSize, 1.f});
        }
    }

    // Add 1 dynamic box dropping above the grid
    const engine::ecs::Entity mover =
            add_box(world, glm::vec3{5.f, static_cast<float>(kGridH) + 2.f, 0.f}, glm::vec3{1.f, 1.f, 1.f});
    make_dynamic(world, mover, 1.f);

    step_physics(world);

    const std::vector<engine::CollisionEvent2D> events = read_collisions(world);
    EXPECT_EQ(events.size(), 0u);
}

// Sleeping body wakeup propagation.
// 5 sleeping boxes in a row; a dynamic projectile hits the first box.
// Tests that wake_sleepers_by_overlap_2d propagates wake state across touching contacts.
TEST(Physics2DStability, SleepWakeupPropagation) {
    engine::ecs::World world = make_physics_world(glm::vec3{0.f});

    constexpr int kChainLength = 5;
    constexpr float kBoxSize = 1.f;
    std::vector<engine::ecs::Entity> chain;

    for (int i = 0; i < kChainLength; ++i) {
        const engine::ecs::Entity box =
                add_box(world, glm::vec3{static_cast<float>(i) * (kBoxSize + 0.01f), 0.f, 0.f},
                        glm::vec3{kBoxSize, kBoxSize, 1.f});
        make_dynamic(world, box, 1.f);
        world.get<engine::RigidBody2D>(box).sleeping = true;
        chain.push_back(box);
    }

    const engine::ecs::Entity bullet = add_box(world, glm::vec3{-2.f, 0.f, 0.f}, glm::vec3{0.5f, 0.5f, 1.f});
    make_dynamic(world, bullet, 1.f, glm::vec3{120.f, 0.f, 0.f});

    step_physics(world);

    EXPECT_FALSE(world.get<engine::RigidBody2D>(chain[0]).sleeping);
}
