#include <gtest/gtest.h>

#include "core/game_window_reset.h"

#include "fixtures/fake_services.h"

TEST(GameWindowReset, ReleasesRelativeMouseOnEveryOpenWindow) {
    fakes::FakeWindowControl windows;
    const auto second = windows.open_window(engine::WindowDesc{});
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(windows.set_relative_mouse(engine::kPrimaryWindow, true));
    ASSERT_TRUE(windows.set_relative_mouse(*second, true));

    engine::reset_game_windows(windows);

    EXPECT_FALSE(windows.is_relative_mouse(engine::kPrimaryWindow));
    EXPECT_FALSE(windows.is_relative_mouse(*second));
}

TEST(GameWindowReset, ReleasesTheGameCapture) {
    fakes::FakeWindowControl windows;
    const auto second = windows.open_window(engine::WindowDesc{});
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(windows.capture_mouse(*second, true));

    engine::reset_game_windows(windows);

    EXPECT_FALSE(windows.is_mouse_captured(*second));
    EXPECT_FALSE(windows.capture_owner.has_value());
}

TEST(GameWindowReset, ClearsDragRegionClickThroughAndOverlayMode) {
    fakes::FakeWindowControl windows;
    windows.set_drag_region(engine::render::Rect{0, 0, 100, 20}, engine::kPrimaryWindow);
    windows.set_click_through_enabled(true, engine::kPrimaryWindow);
    windows.set_overlay_mode(engine::OverlayMode::AlwaysDisabled);

    engine::reset_game_windows(windows);

    EXPECT_FALSE(windows.drag_regions.at(engine::kPrimaryWindow).has_value());
    EXPECT_FALSE(windows.click_through.at(engine::kPrimaryWindow));
    EXPECT_EQ(windows.overlay_mode(), engine::OverlayMode::Auto);
}
