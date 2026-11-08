#include <gtest/gtest.h>

#include <engine/nav/string_puller.h>

#include <set>
#include <vector>

namespace {

using namespace engine::nav;

TEST(NavStringPuller, SegmentClear2D) {
    std::set<std::pair<int, int>> blocked;
    auto is_walkable = [&](glm::ivec2 cell) {
        return !blocked.contains({cell.x, cell.y});
    };

    EXPECT_TRUE(segment_clear_2d({0, 0}, {5, 5}, is_walkable));

    // Put obstacle right on the path
    blocked.insert({2, 2});
    EXPECT_FALSE(segment_clear_2d({0, 0}, {5, 5}, is_walkable));

    // Corner cutting prevention: stepping from (0,0) to (1,1) when (0,1) is blocked
    blocked.clear();
    blocked.insert({0, 1});
    EXPECT_FALSE(segment_clear_2d({0, 0}, {1, 1}, is_walkable));
}

TEST(NavStringPuller, SmoothPath2D) {
    std::set<std::pair<int, int>> blocked;
    auto is_walkable = [&](glm::ivec2 cell) {
        return !blocked.contains({cell.x, cell.y});
    };

    // A staircase zigzag from (0,0) to (3,3) in an open field:
    // (0,0) -> (1,0) -> (1,1) -> (2,1) -> (2,2) -> (3,2) -> (3,3)
    const std::vector<glm::ivec2> zigzag{
            {0, 0}, {1, 0}, {1, 1}, {2, 1}, {2, 2}, {3, 2}, {3, 3}};

    const auto smoothed = smooth_path_2d(zigzag, is_walkable);
    ASSERT_EQ(smoothed.size(), 2u);
    EXPECT_EQ(smoothed.front(), (glm::ivec2{0, 0}));
    EXPECT_EQ(smoothed.back(), (glm::ivec2{3, 3}));
}

TEST(NavStringPuller, SmoothPathAroundCorner) {
    std::set<std::pair<int, int>> blocked;
    auto is_walkable = [&](glm::ivec2 cell) {
        return !blocked.contains({cell.x, cell.y});
    };

    // Block a wall between x=1..2 at y=1..3
    blocked.insert({2, 1});
    blocked.insert({2, 2});

    // Path goes around the corner: (0,0) -> (1,0) -> (3,0) -> (3,3)
    const std::vector<glm::ivec2> path{
            {0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}, {3, 2}, {3, 3}};

    const auto smoothed = smooth_path_2d(path, is_walkable);
    // Should preserve the corner waypoint at (3,0)
    ASSERT_GE(smoothed.size(), 3u);
    EXPECT_EQ(smoothed.front(), (glm::ivec2{0, 0}));
    EXPECT_EQ(smoothed.back(), (glm::ivec2{3, 3}));
    EXPECT_EQ(smoothed[1], (glm::ivec2{3, 0}));
}

} // namespace
