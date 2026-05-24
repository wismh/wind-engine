#include <gtest/gtest.h>

#include <engine/core/window_control.h>

#include "fixtures/fake_services.h"

TEST(WindowCursor, UnavailableWhereThePlatformCannotSay) {
    fakes::FakeWindowControl windows;
    EXPECT_FALSE(windows.cursor_screen_position().has_value());
    EXPECT_FALSE(windows.is_mouse_button_down(engine::MouseButton::Left));
    EXPECT_FALSE(engine::cursor_client_position(windows, engine::kPrimaryWindow).has_value());
}

TEST(WindowCursor, ButtonStateFollowsTheOs) {
    fakes::FakeWindowControl windows;
    windows.cursor = glm::vec2{10.0f, 20.0f};
    windows.buttons_down = {engine::MouseButton::Right};

    EXPECT_FALSE(windows.is_mouse_button_down(engine::MouseButton::Left));
    EXPECT_TRUE(windows.is_mouse_button_down(engine::MouseButton::Right));
    EXPECT_FALSE(windows.is_mouse_button_down(engine::MouseButton::None));
}

TEST(WindowCursor, ClientPositionIsTheScreenPositionMinusTheWindowOrigin) {
    fakes::FakeWindowControl windows;
    const auto second = windows.open_window(engine::WindowDesc{.position = glm::ivec2{500, 300}});
    ASSERT_TRUE(second.has_value());
    windows.set_position({100, 50}, engine::kPrimaryWindow);

    windows.cursor = glm::vec2{620.0f, 340.0f};
    EXPECT_EQ(engine::cursor_client_position(windows, *second), (glm::vec2{120.0f, 40.0f}));
    EXPECT_EQ(engine::cursor_client_position(windows, engine::kPrimaryWindow), (glm::vec2{520.0f, 290.0f}));

    // Left of and above the window: negative, so a drag that left the window keeps its direction.
    windows.cursor = glm::vec2{400.0f, 250.0f};
    EXPECT_EQ(engine::cursor_client_position(windows, *second), (glm::vec2{-100.0f, -50.0f}));

    EXPECT_FALSE(engine::cursor_client_position(windows, engine::WindowId{77}).has_value());
}

TEST(WindowCursor, CaptureBelongsToOneOpenWindowAndEndsWhenItCloses) {
    fakes::FakeWindowControl windows;
    const auto second = windows.open_window(engine::WindowDesc{});
    ASSERT_TRUE(second.has_value());

    EXPECT_TRUE(windows.capture_mouse(*second, true));
    EXPECT_TRUE(windows.is_mouse_captured(*second));
    EXPECT_FALSE(windows.is_mouse_captured(engine::kPrimaryWindow));

    EXPECT_TRUE(windows.capture_mouse(engine::kPrimaryWindow, true));
    EXPECT_FALSE(windows.is_mouse_captured(*second));

    windows.close_window(engine::kPrimaryWindow);
    EXPECT_FALSE(windows.is_mouse_captured(engine::kPrimaryWindow));

    EXPECT_FALSE(windows.capture_mouse(engine::WindowId{42}, true));
}

TEST(WindowCursor, CaptureIsRefusedWhereTheSystemCannotCapture) {
    fakes::FakeWindowControl windows;
    windows.capture_supported = false;
    EXPECT_FALSE(windows.capture_mouse(engine::kPrimaryWindow, true));
    EXPECT_FALSE(windows.is_mouse_captured(engine::kPrimaryWindow));
    EXPECT_FALSE(windows.capture_mouse(engine::kPrimaryWindow, false));
}

TEST(WindowControl, TracksWindowStateAndVisibilityInFake) {
    std::vector<std::string> log;
    fakes::FakeWindowControl control;
    control.log = &log;
    const auto win = control.open_window(engine::WindowDesc{.title = "Test"});
    ASSERT_TRUE(win.has_value());

    EXPECT_TRUE(control.is_visible(*win));
    EXPECT_FALSE(control.is_maximized(*win));
    EXPECT_FALSE(control.is_minimized(*win));

    control.maximize(*win);
    EXPECT_TRUE(control.is_maximized(*win));
    EXPECT_FALSE(control.is_minimized(*win));

    control.minimize(*win);
    EXPECT_FALSE(control.is_maximized(*win));
    EXPECT_TRUE(control.is_minimized(*win));

    control.restore(*win);
    EXPECT_FALSE(control.is_maximized(*win));
    EXPECT_FALSE(control.is_minimized(*win));

    control.hide(*win);
    EXPECT_FALSE(control.is_visible(*win));

    control.show(*win);
    EXPECT_TRUE(control.is_visible(*win));

    control.close_window(*win);
    EXPECT_FALSE(control.is_visible(*win));
    EXPECT_FALSE(control.is_maximized(*win));
}

