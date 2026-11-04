#include <gtest/gtest.h>

#include "net/relevance_grid.h"

#include <cstdint>
#include <vector>

namespace engine::net {
namespace {

TEST(RelevanceGrid, CellOfFloorsNegativeCoordinates) {
    EXPECT_EQ(RelevanceGrid::cell_of({0.5f, 9.9f, 10.0f}, 10.0f), glm::ivec3(0, 0, 1));
    EXPECT_EQ(RelevanceGrid::cell_of({-0.5f, -10.0f, -10.5f}, 10.0f), glm::ivec3(-1, -1, -2));
}

TEST(RelevanceGrid, DistanceIsChebyshev) {
    EXPECT_EQ(RelevanceGrid::distance({0, 0, 0}, {0, 0, 0}), 0);
    EXPECT_EQ(RelevanceGrid::distance({1, -2, 3}, {-1, 1, 3}), 3);
}

TEST(RelevanceGrid, MarksOnlyWithinTheRadius) {
    RelevanceGrid grid;
    grid.rebuild({
            {.cell = {0, 0, 0}, .index = 0},
            {.cell = {2, 0, 0}, .index = 1},
            {.cell = {3, 0, 0}, .index = 2},
            {.cell = {0, -2, 2}, .index = 3},
            {.cell = {0, 0, -3}, .index = 4},
            {.cell = {0, 0, 0}, .index = 5},
    });
    std::vector<std::uint8_t> marks(6, 0);
    grid.mark_around({0, 0, 0}, 2, marks);
    EXPECT_EQ(marks, (std::vector<std::uint8_t>{1, 1, 0, 1, 0, 1}));
}

TEST(RelevanceGrid, ProbingAndScanningAgree) {
    RelevanceGrid grid;
    std::vector<RelevanceGrid::Item> items;
    std::uint32_t index = 0;
    for (int x = -6; x <= 6; ++x) {
        for (int y = -6; y <= 6; y += 2) {
            for (int z = -6; z <= 6; z += 3) {
                items.push_back({.cell = {x, y, z}, .index = index++});
            }
        }
    }
    grid.rebuild(std::move(items));
    for (const glm::ivec3 center : {glm::ivec3(0, 0, 0), glm::ivec3(-5, 3, 2), glm::ivec3(100, 0, 0)}) {
        for (const int radius : {0, 1, 3}) {
            std::vector<std::uint8_t> probed(index, 0);
            std::vector<std::uint8_t> scanned(index, 0);
            std::vector<std::uint8_t> chosen(index, 0);
            grid.mark_by_probing(center, radius, probed);
            grid.mark_by_scanning(center, radius, scanned);
            grid.mark_around(center, radius, chosen);
            EXPECT_EQ(probed, scanned);
            EXPECT_EQ(probed, chosen);
        }
    }
}

} // namespace
} // namespace engine::net
