#include <gtest/gtest.h>

#include <engine/nav/a_star.h>

#include <array>
#include <set>

namespace {

using namespace engine::nav;

// Simple 2D test grid graph.
struct SimpleGrid2D {
    std::set<std::pair<int, int>> obstacles;
    int max_x = 20;
    int max_y = 20;

    [[nodiscard]] float heuristic(glm::ivec2 a, glm::ivec2 b) const noexcept {
        return Heuristic::manhattan_2d(a, b);
    }

    template <typename Visitor>
    void neighbors(glm::ivec2 node, Visitor&& visitor) const {
        static constexpr std::array<glm::ivec2, 4> kOffsets{
                glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1}, glm::ivec2{0, -1}};
        for (const auto& offset : kOffsets) {
            const glm::ivec2 next = node + offset;
            if (next.x < 0 || next.x > max_x || next.y < 0 || next.y > max_y) {
                continue;
            }
            if (obstacles.contains({next.x, next.y})) {
                continue;
            }
            visitor(next, 1.0f);
        }
    }
};

TEST(NavAStar, DirectPath) {
    SimpleGrid2D grid;
    const glm::ivec2 start{0, 0};
    const glm::ivec2 goal{3, 0};

    const auto path = a_star_find_path(grid, start, goal);
    EXPECT_TRUE(path.complete());
    EXPECT_EQ(path.nodes.size(), 3u);
    EXPECT_EQ(path.total_cost, 3.0f);
    EXPECT_EQ(path.nodes.back(), goal);
}

TEST(NavAStar, StartEqualsGoal) {
    SimpleGrid2D grid;
    const glm::ivec2 start{2, 2};

    const auto path_no_start = a_star_find_path(grid, start, start, {.include_start = false});
    EXPECT_TRUE(path_no_start.complete());
    EXPECT_TRUE(path_no_start.empty());

    const auto path_with_start = a_star_find_path(grid, start, start, {.include_start = true});
    EXPECT_TRUE(path_with_start.complete());
    ASSERT_EQ(path_with_start.nodes.size(), 1u);
    EXPECT_EQ(path_with_start.nodes.front(), start);
}

TEST(NavAStar, ObstacleAvoidance) {
    SimpleGrid2D grid;
    // Obstacle wall at x = 2 from y = 0 to y = 2
    grid.obstacles.insert({2, 0});
    grid.obstacles.insert({2, 1});
    grid.obstacles.insert({2, 2});

    const glm::ivec2 start{1, 0};
    const glm::ivec2 goal{3, 0};

    const auto path = a_star_find_path(grid, start, goal);
    EXPECT_TRUE(path.complete());
    EXPECT_EQ(path.nodes.back(), goal);
    for (const auto& node : path.nodes) {
        EXPECT_FALSE(grid.obstacles.contains({node.x, node.y}));
    }
}

TEST(NavAStar, BudgetExhaustionPartialFallback) {
    SimpleGrid2D grid;
    const glm::ivec2 start{0, 0};
    const glm::ivec2 goal{20, 20};

    // Very tight budget
    NavSearchParams params;
    params.budget = 10;
    params.allow_partial = true;

    const auto path = a_star_find_path(grid, start, goal, params);
    EXPECT_TRUE(path.partial());
    EXPECT_FALSE(path.empty());
    // The path should move closer towards (20, 20)
    EXPECT_GT(path.nodes.back().x + path.nodes.back().y, start.x + start.y);
}

TEST(NavAStar, UnreachableNoPartial) {
    SimpleGrid2D grid;
    // Box around goal
    grid.obstacles.insert({9, 10});
    grid.obstacles.insert({11, 10});
    grid.obstacles.insert({10, 9});
    grid.obstacles.insert({10, 11});

    const glm::ivec2 start{0, 0};
    const glm::ivec2 goal{10, 10};

    NavSearchParams params;
    params.allow_partial = false;

    const auto path = a_star_find_path(grid, start, goal, params);
    EXPECT_EQ(path.status, PathStatus::NotFound);
    EXPECT_TRUE(path.empty());
}

} // namespace
