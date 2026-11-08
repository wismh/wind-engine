#include <gtest/gtest.h>

#include <engine/nav/a_star.h>
#include <engine/nav/grid_graph_2d.h>
#include <engine/nav/grid_graph_3d.h>

#include <set>

namespace {

using namespace engine::nav;

TEST(NavGridGraph, GridGraph2DNeighborsAndBounds) {
    std::set<std::pair<int, int>> blocked;
    auto is_walkable = [&](glm::ivec2 c) {
        return !blocked.contains({c.x, c.y});
    };

    GridGraph2D graph(is_walkable, true, false);
    graph.set_bounds({0, 0}, {5, 5});

    // Check corner cell (0,0) - should have only 3 neighbors (right, up, up-right)
    int count = 0;
    graph.neighbors({0, 0}, [&](glm::ivec2 n, float cost) {
        ++count;
        EXPECT_GE(n.x, 0);
        EXPECT_GE(n.y, 0);
        EXPECT_LE(n.x, 5);
        EXPECT_LE(n.y, 5);
        EXPECT_GT(cost, 0.0f);
    });
    EXPECT_EQ(count, 3);
}

TEST(NavGridGraph, GridGraph2DAStarIntegration) {
    std::set<std::pair<int, int>> blocked;
    // L-shaped wall
    blocked.insert({1, 0});
    blocked.insert({1, 1});

    auto is_walkable = [&](glm::ivec2 c) {
        return !blocked.contains({c.x, c.y});
    };

    GridGraph2D graph(is_walkable, true, false);
    graph.set_bounds({0, 0}, {4, 4});

    const auto path = a_star_find_path(graph, glm::ivec2{0, 0}, glm::ivec2{2, 0});
    EXPECT_TRUE(path.complete());
    EXPECT_EQ(path.nodes.back(), (glm::ivec2{2, 0}));
}

TEST(NavGridGraph, GridGraph3DSteppingAndDrops) {
    // Floor at y=0, ledge at (1, 1, 0) y=1, pit at (2, -1, 0) y=-1
    std::set<std::tuple<int, int, int>> standable{
            {0, 0, 0},
            {1, 1, 0},   // 1 block up
            {2, -1, 0},  // 2 blocks down from ledge
    };

    auto can_stand = [&](glm::ivec3 c) {
        return standable.contains({c.x, c.y, c.z});
    };

    GridGraph3D graph(can_stand, 1, 3, false);

    const auto path = a_star_find_path(graph, glm::ivec3{0, 0, 0}, glm::ivec3{2, -1, 0});
    EXPECT_TRUE(path.complete());
    ASSERT_EQ(path.nodes.size(), 2u);
    EXPECT_EQ(path.nodes[0], (glm::ivec3{1, 1, 0}));
    EXPECT_EQ(path.nodes[1], (glm::ivec3{2, -1, 0}));
}

} // namespace
