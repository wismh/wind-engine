#include <gtest/gtest.h>

#include <engine/nav/flow_field_2d.h>
#include <engine/nav/flow_field_3d.h>

#include <set>
#include <vector>

namespace {

using namespace engine::nav;

TEST(NavFlowField, FlowField2DSingleGoal) {
    FlowField2D field;
    field.init_grid({0, 0}, {10, 10});

    auto is_walkable = [](glm::ivec2) { return true; };

    const glm::ivec2 goal{5, 5};
    const std::vector<glm::ivec2> goals{goal};
    field.build(goals, is_walkable, 50.0f, true);

    EXPECT_TRUE(field.has_flow(goal));
    EXPECT_FLOAT_EQ(field.cost(goal), 0.0f);

    // Cell (5, 3) should point +Y towards (5, 5)
    EXPECT_TRUE(field.has_flow({5, 3}));
    const glm::vec2 dir = field.direction({5, 3});
    EXPECT_NEAR(dir.x, 0.0f, 1e-3f);
    EXPECT_GT(dir.y, 0.5f);

    // Follow path to goal
    const auto path = field.path_to_goal({5, 2});
    EXPECT_FALSE(path.empty());
    EXPECT_EQ(path.back(), goal);
}

TEST(NavFlowField, FlowField2DObstacleAvoidance) {
    FlowField2D field;
    field.init_grid({0, 0}, {10, 10});

    // Wall at x=5, y=3..7
    std::set<std::pair<int, int>> blocked;
    for (int y = 3; y <= 7; ++y) {
        blocked.insert({5, y});
    }

    auto is_walkable = [&](glm::ivec2 c) {
        return !blocked.contains({c.x, c.y});
    };

    const glm::ivec2 goal{8, 5};
    const std::vector<glm::ivec2> goals{goal};
    field.build(goals, is_walkable, 50.0f, true);

    // Cell (2, 5) is behind the wall; path must go around the wall
    const auto path = field.path_to_goal({2, 5});
    EXPECT_FALSE(path.empty());
    EXPECT_EQ(path.back(), goal);
    for (const auto& step : path) {
        EXPECT_FALSE(blocked.contains({step.x, step.y}));
    }
}

TEST(NavFlowField, FlowField2DOkResetRecycling) {
    FlowField2D field;
    field.init_grid({0, 0}, {20, 20});

    auto is_walkable = [](glm::ivec2) { return true; };

    // First query with small radius
    const std::vector<glm::ivec2> goals1{{5, 5}};
    field.build(goals1, is_walkable, 5.0f);
    EXPECT_TRUE(field.has_flow({5, 5}));
    EXPECT_TRUE(field.has_flow({5, 6}));

    // Reset and build at a completely different location
    const std::vector<glm::ivec2> goals2{{15, 15}};
    field.build(goals2, is_walkable, 5.0f);

    // Old location should no longer have flow!
    EXPECT_FALSE(field.has_flow({5, 5}));
    EXPECT_FALSE(field.has_flow({5, 6}));
    // New location has flow
    EXPECT_TRUE(field.has_flow({15, 15}));
}

TEST(NavFlowField, FlowField3DSteppedTerrain) {
    FlowField3D field;
    field.init_grid({0, 0, 0}, {10, 5, 10});

    // Floor at y=1, ledge at y=2
    std::set<std::tuple<int, int, int>> standable;
    for (int x = 0; x < 10; ++x) {
        for (int z = 0; z < 10; ++z) {
            const int y = (x >= 5) ? 2 : 1;
            standable.insert({x, y, z});
        }
    }

    auto can_stand = [&](glm::ivec3 c) {
        return standable.contains({c.x, c.y, c.z});
    };

    const glm::ivec3 goal{7, 2, 5};
    const std::vector<glm::ivec3> goals{goal};
    field.build(goals, can_stand, 1, 3, 30.0f);

    EXPECT_TRUE(field.has_flow(goal));
    // Walker at (2, 1, 5) steps up to (5, 2, 5) and reaches goal
    EXPECT_TRUE(field.has_flow({2, 1, 5}));
    const auto path = field.path_to_goal({2, 1, 5});
    EXPECT_FALSE(path.empty());
    EXPECT_EQ(path.back(), goal);
}

} // namespace
