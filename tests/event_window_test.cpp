#include <gtest/gtest.h>

#include "core/event_window.h"

#include <optional>

// core/event_window.h: which window an SDL event goes to. docs/tech/features/Windowing.md#events-of-a-window.

namespace {

using engine::event_window;
using engine::kPrimaryWindow;
using engine::NoWindowEvent;
using engine::WindowId;

constexpr WindowId kFloatWindow{3};

}

TEST(EventWindow, AnIdOfALiveWindowGoesToThatWindow) {
    EXPECT_EQ(event_window(7, kFloatWindow, NoWindowEvent::Primary), kFloatWindow);
    EXPECT_EQ(event_window(7, kFloatWindow, NoWindowEvent::Drop), kFloatWindow);
    EXPECT_EQ(event_window(2, kPrimaryWindow, NoWindowEvent::Drop), kPrimaryWindow);
}

TEST(EventWindow, AnIdOfAClosedWindowIsDroppedNotSentToThePrimaryWindow) {
    // Input still queued for a dock float window that closed this frame.
    EXPECT_EQ(event_window(7, std::nullopt, NoWindowEvent::Primary), std::nullopt);
    EXPECT_EQ(event_window(7, std::nullopt, NoWindowEvent::Drop), std::nullopt);
}

TEST(EventWindow, IdZeroIsThePrimaryWindowForInputAndNothingForWindowEvents) {
    EXPECT_EQ(event_window(0, std::nullopt, NoWindowEvent::Primary), kPrimaryWindow);
    EXPECT_EQ(event_window(0, std::nullopt, NoWindowEvent::Drop), std::nullopt);
}
