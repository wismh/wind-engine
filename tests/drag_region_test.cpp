#include <gtest/gtest.h>

#if defined(ENGINE_WITH_WINDOW)
#include "render/opengl/window_system.h"

#include <vector>

TEST(DragRegion, NoRegionIsNeverADrag) {
    EXPECT_FALSE(engine::drag_region_contains(std::nullopt, {}, {5.0f, 5.0f}));
}

TEST(DragRegion, TheRegionIsHalfOpen) {
    const std::optional<engine::render::Rect> region = engine::render::Rect{10.0f, 10.0f, 100.0f, 30.0f};
    EXPECT_TRUE(engine::drag_region_contains(region, {}, {10.0f, 10.0f}));
    EXPECT_TRUE(engine::drag_region_contains(region, {}, {109.9f, 39.9f}));
    EXPECT_FALSE(engine::drag_region_contains(region, {}, {110.0f, 20.0f}));
    EXPECT_FALSE(engine::drag_region_contains(region, {}, {50.0f, 40.0f}));
    EXPECT_FALSE(engine::drag_region_contains(region, {}, {9.9f, 20.0f}));
}

TEST(DragRegion, AnExclusionIsNotADragAndTheRestStillIs) {
    // A title bar 300 wide with a close button at its right end.
    const std::optional<engine::render::Rect> region = engine::render::Rect{0.0f, 0.0f, 300.0f, 30.0f};
    const std::vector<engine::render::Rect> holes{{260.0f, 4.0f, 24.0f, 22.0f}};

    EXPECT_TRUE(engine::drag_region_contains(region, holes, {100.0f, 15.0f}));
    EXPECT_FALSE(engine::drag_region_contains(region, holes, {270.0f, 15.0f}));
    EXPECT_FALSE(engine::drag_region_contains(region, holes, {260.0f, 4.0f}));
    EXPECT_TRUE(engine::drag_region_contains(region, holes, {290.0f, 15.0f}));
    EXPECT_TRUE(engine::drag_region_contains(region, holes, {270.0f, 2.0f}));
}

TEST(DragRegion, SeveralExclusionsAndOneOutsideTheRegion) {
    const std::optional<engine::render::Rect> region = engine::render::Rect{0.0f, 0.0f, 300.0f, 30.0f};
    const std::vector<engine::render::Rect> holes{{10.0f, 0.0f, 20.0f, 30.0f}, {100.0f, 0.0f, 20.0f, 30.0f},
            {500.0f, 500.0f, 10.0f, 10.0f}};

    EXPECT_FALSE(engine::drag_region_contains(region, holes, {15.0f, 10.0f}));
    EXPECT_FALSE(engine::drag_region_contains(region, holes, {110.0f, 10.0f}));
    EXPECT_TRUE(engine::drag_region_contains(region, holes, {60.0f, 10.0f}));
    EXPECT_FALSE(engine::drag_region_contains(region, holes, {505.0f, 505.0f}));
}

#endif
