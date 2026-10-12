#include <gtest/gtest.h>

#if defined(ENGINE_WITH_WINDOW)
#include "render/opengl/mouse_capture.h"

namespace {

bool g_supported = true;
bool g_state = false;
int g_calls = 0;

bool fake_apply(bool on) {
    ++g_calls;
    if (!g_supported) {
        return false;
    }
    g_state = on;
    return true;
}

class MouseCaptureTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_supported = true;
        g_state = false;
        g_calls = 0;
    }
};

const engine::WindowId kA = engine::kPrimaryWindow;
const engine::WindowId kB{1};

}

TEST_F(MouseCaptureTest, AGameCaptureIsOnUntilReleased) {
    engine::MouseCapture capture(&fake_apply);
    EXPECT_FALSE(capture.active());

    EXPECT_TRUE(capture.capture(kA, true));
    EXPECT_TRUE(g_state);
    EXPECT_TRUE(capture.captured_by(kA));
    EXPECT_FALSE(capture.captured_by(kB));

    EXPECT_FALSE(capture.capture(kA, false));
    EXPECT_FALSE(g_state);
    EXPECT_FALSE(capture.captured_by(kA));
    EXPECT_FALSE(capture.active());
}

TEST_F(MouseCaptureTest, ReleasingAWindowThatDoesNotHoldItChangesNothing) {
    engine::MouseCapture capture(&fake_apply);
    ASSERT_TRUE(capture.capture(kA, true));
    EXPECT_FALSE(capture.capture(kB, false));
    EXPECT_TRUE(g_state);
    EXPECT_TRUE(capture.captured_by(kA));
}

TEST_F(MouseCaptureTest, CapturingAnotherWindowMovesTheCapture) {
    engine::MouseCapture capture(&fake_apply);
    ASSERT_TRUE(capture.capture(kA, true));
    ASSERT_TRUE(capture.capture(kB, true));
    EXPECT_FALSE(capture.captured_by(kA));
    EXPECT_TRUE(capture.captured_by(kB));
    EXPECT_TRUE(g_state);
    // The OS capture stayed on: it was not switched off and on again.
    EXPECT_EQ(g_calls, 1);
}

TEST_F(MouseCaptureTest, TheEnginesWindowDragDoesNotSwitchOffAGameCapture) {
    engine::MouseCapture capture(&fake_apply);
    ASSERT_TRUE(capture.capture(kA, true));

    capture.begin_window_drag();
    capture.end_window_drag();

    EXPECT_TRUE(g_state);
    EXPECT_TRUE(capture.captured_by(kA));
}

TEST_F(MouseCaptureTest, AGameReleaseDoesNotSwitchOffTheEnginesWindowDrag) {
    engine::MouseCapture capture(&fake_apply);
    capture.begin_window_drag();
    ASSERT_TRUE(g_state);
    ASSERT_TRUE(capture.capture(kA, true));

    EXPECT_FALSE(capture.capture(kA, false));
    EXPECT_TRUE(g_state);  // the drag still needs it

    capture.end_window_drag();
    EXPECT_FALSE(g_state);
}

TEST_F(MouseCaptureTest, AnUnsupportedPlatformRefusesAndHoldsNothing) {
    g_supported = false;
    engine::MouseCapture capture(&fake_apply);

    EXPECT_FALSE(capture.capture(kA, true));
    EXPECT_FALSE(capture.captured_by(kA));
    EXPECT_FALSE(capture.active());
    // The engine's own drag still runs without a capture.
    capture.begin_window_drag();
    capture.end_window_drag();
    EXPECT_FALSE(capture.active());
}

TEST_F(MouseCaptureTest, ClosingTheWindowReleasesItsCapture) {
    engine::MouseCapture capture(&fake_apply);
    ASSERT_TRUE(capture.capture(kB, true));
    capture.window_closed(kA);
    EXPECT_TRUE(capture.captured_by(kB));
    capture.window_closed(kB);
    EXPECT_FALSE(capture.captured_by(kB));
    EXPECT_FALSE(g_state);
}

TEST_F(MouseCaptureTest, AReleaseTheSystemReportsAsFailedIsStillARelease) {
    engine::MouseCapture capture(&fake_apply);
    ASSERT_TRUE(capture.capture(kA, true));
    g_supported = false;  // from now on SDL_CaptureMouse answers false

    EXPECT_FALSE(capture.capture(kA, false));
    EXPECT_FALSE(capture.captured_by(kA));
    EXPECT_FALSE(capture.active());
}

TEST_F(MouseCaptureTest, UnbalancedEndOfADragIsHarmless) {
    engine::MouseCapture capture(&fake_apply);
    capture.end_window_drag();
    EXPECT_FALSE(capture.active());
    capture.begin_window_drag();
    EXPECT_TRUE(g_state);
}

TEST(GlobalMouse, WaylandTheBrowserAndAndroidAreUnsupported) {
    EXPECT_TRUE(engine::global_mouse_supported("windows"));
    EXPECT_TRUE(engine::global_mouse_supported("x11"));
    EXPECT_TRUE(engine::global_mouse_supported("cocoa"));
    EXPECT_FALSE(engine::global_mouse_supported("wayland"));
    EXPECT_FALSE(engine::global_mouse_supported("emscripten"));
    EXPECT_FALSE(engine::global_mouse_supported("android"));
    EXPECT_FALSE(engine::global_mouse_supported(""));
}

#endif
