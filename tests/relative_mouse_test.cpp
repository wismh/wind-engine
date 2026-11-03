#include <gtest/gtest.h>

#include "core/relative_mouse.h"

#include <utility>
#include <vector>

namespace {

// Records every switch of the OS mode and answers `accept`.
struct Recorder {
    std::vector<std::pair<engine::WindowId, bool>> calls;
    bool accept = true;

    engine::RelativeMouse::Apply apply() {
        return [this](engine::WindowId window, bool on) {
            calls.emplace_back(window, on);
            return accept;
        };
    }
};

const engine::WindowId kA = engine::kPrimaryWindow;
const engine::WindowId kB{1};

}

TEST(RelativeMouse, RequestAppliesAndIsRemembered) {
    Recorder os;
    engine::RelativeMouse mouse(os.apply());

    EXPECT_TRUE(mouse.request(kA, true));

    EXPECT_TRUE(mouse.requested(kA));
    EXPECT_TRUE(mouse.active(kA));
    EXPECT_FALSE(mouse.requested(kB));
    ASSERT_EQ(os.calls.size(), 1u);
    EXPECT_EQ(os.calls[0], std::make_pair(kA, true));
}

TEST(RelativeMouse, RefusedRequestIsNotRemembered) {
    Recorder os;
    os.accept = false;
    engine::RelativeMouse mouse(os.apply());

    EXPECT_FALSE(mouse.request(kA, true));

    EXPECT_FALSE(mouse.requested(kA));
    EXPECT_FALSE(mouse.active(kA));
}

TEST(RelativeMouse, ReleaseForgetsAndSwitchesOff) {
    Recorder os;
    engine::RelativeMouse mouse(os.apply());
    mouse.request(kA, true);

    EXPECT_FALSE(mouse.request(kA, false));

    EXPECT_FALSE(mouse.requested(kA));
    EXPECT_FALSE(mouse.active(kA));
    ASSERT_EQ(os.calls.size(), 2u);
    EXPECT_EQ(os.calls[1], std::make_pair(kA, false));

    // Releasing again, or a window that never asked, does not touch the OS.
    mouse.request(kA, false);
    mouse.request(kB, false);
    EXPECT_EQ(os.calls.size(), 2u);
}

TEST(RelativeMouse, FocusLossReleasesAndKeepsTheRequest) {
    Recorder os;
    engine::RelativeMouse mouse(os.apply());
    mouse.request(kA, true);

    mouse.focus_lost(kA);

    EXPECT_TRUE(mouse.requested(kA));
    EXPECT_FALSE(mouse.active(kA));
    ASSERT_EQ(os.calls.size(), 2u);
    EXPECT_EQ(os.calls[1], std::make_pair(kA, false));

    // A second loss changes nothing.
    mouse.focus_lost(kA);
    EXPECT_EQ(os.calls.size(), 2u);
}

TEST(RelativeMouse, FocusGainRestoresARequest) {
    Recorder os;
    engine::RelativeMouse mouse(os.apply());
    mouse.request(kA, true);
    mouse.focus_lost(kA);

    mouse.focus_gained(kA);

    EXPECT_TRUE(mouse.active(kA));
    ASSERT_EQ(os.calls.size(), 3u);
    EXPECT_EQ(os.calls[2], std::make_pair(kA, true));

    // Already on: a second gain does not apply again.
    mouse.focus_gained(kA);
    EXPECT_EQ(os.calls.size(), 3u);
}

TEST(RelativeMouse, FocusGainOfAWindowThatDidNotAskChangesNothing) {
    Recorder os;
    engine::RelativeMouse mouse(os.apply());
    mouse.request(kA, true);

    mouse.focus_gained(kB);
    mouse.focus_lost(kB);

    EXPECT_FALSE(mouse.active(kB));
    EXPECT_TRUE(mouse.active(kA));
    EXPECT_EQ(os.calls.size(), 1u);
}

TEST(RelativeMouse, ReleaseWhileUnfocusedDoesNotGainItBack) {
    Recorder os;
    engine::RelativeMouse mouse(os.apply());
    mouse.request(kA, true);
    mouse.focus_lost(kA);

    mouse.request(kA, false);
    mouse.focus_gained(kA);

    EXPECT_FALSE(mouse.active(kA));
    EXPECT_FALSE(mouse.requested(kA));
    EXPECT_EQ(os.calls.size(), 2u);
}

TEST(RelativeMouse, RefusedFocusGainKeepsTheRequestForTheNextGain) {
    Recorder os;
    engine::RelativeMouse mouse(os.apply());
    mouse.request(kA, true);
    mouse.focus_lost(kA);

    os.accept = false;
    mouse.focus_gained(kA);
    EXPECT_TRUE(mouse.requested(kA));
    EXPECT_FALSE(mouse.active(kA));

    os.accept = true;
    mouse.focus_gained(kA);
    EXPECT_TRUE(mouse.active(kA));
}

TEST(RelativeMouse, WindowsAreIndependent) {
    Recorder os;
    engine::RelativeMouse mouse(os.apply());
    mouse.request(kA, true);
    mouse.request(kB, true);

    mouse.focus_lost(kA);

    EXPECT_FALSE(mouse.active(kA));
    EXPECT_TRUE(mouse.active(kB));
}

TEST(RelativeMouse, ClosedWindowIsForgottenWithoutApplying) {
    Recorder os;
    engine::RelativeMouse mouse(os.apply());
    mouse.request(kB, true);

    mouse.window_closed(kB);

    EXPECT_FALSE(mouse.requested(kB));
    EXPECT_FALSE(mouse.active(kB));
    EXPECT_EQ(os.calls.size(), 1u);
    mouse.focus_gained(kB);
    EXPECT_EQ(os.calls.size(), 1u);
}
