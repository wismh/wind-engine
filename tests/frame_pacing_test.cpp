#include <gtest/gtest.h>

#include "core/frame_limiter.h"
#include "core/frame_pacing.h"

#include <chrono>
#include <limits>
#include <vector>

namespace {

using Ms = std::chrono::milliseconds;
using TimePoint = engine::FrameLimiter::Clock::time_point;

constexpr engine::WindowId kEditorWindow{1};
constexpr engine::WindowId kToolWindow{2};

engine::PacingWindow window(engine::WindowId id, bool presentable = true, bool vsync_supported = true) {
    return engine::PacingWindow{.id = id, .presentable = presentable, .vsync_supported = vsync_supported};
}

}

TEST(FramePacing, PrimaryWindowPacesWhenItCan) {
    const std::vector<engine::PacingWindow> windows{window(kToolWindow), window(engine::kPrimaryWindow),
            window(kEditorWindow)};
    EXPECT_EQ(engine::choose_vsync_window(windows), engine::kPrimaryWindow);
}

TEST(FramePacing, OnlyOneWindowPacesSoATickWaitsOnce) {
    const std::vector<engine::PacingWindow> windows{window(kToolWindow), window(kEditorWindow)};
    EXPECT_EQ(engine::choose_vsync_window(windows), kEditorWindow);
}

TEST(FramePacing, HiddenOrMinimizedPrimaryHandsPacingToAnotherWindow) {
    const std::vector<engine::PacingWindow> windows{window(engine::kPrimaryWindow, false), window(kToolWindow)};
    EXPECT_EQ(engine::choose_vsync_window(windows), kToolWindow);
}

TEST(FramePacing, WindowWithoutSwapIntervalNeverPaces) {
    const std::vector<engine::PacingWindow> windows{window(engine::kPrimaryWindow, true, false),
            window(kEditorWindow)};
    EXPECT_EQ(engine::choose_vsync_window(windows), kEditorWindow);
}

TEST(FramePacing, NoWindowCanPaceLeavesItToTheLimiter) {
    EXPECT_EQ(engine::choose_vsync_window({}), std::nullopt);
    const std::vector<engine::PacingWindow> windows{window(engine::kPrimaryWindow, false),
            window(kEditorWindow, true, false)};
    EXPECT_EQ(engine::choose_vsync_window(windows), std::nullopt);
}

TEST(FramePacing, PeriodFollowsTheRefreshRate) {
    EXPECT_EQ(engine::frame_period(144.0f), std::chrono::nanoseconds{6'944'444});
    EXPECT_EQ(engine::frame_period(60.0f), std::chrono::nanoseconds{16'666'667});
}

TEST(FramePacing, UnknownRefreshRateCountsAsSixtyHertz) {
    const std::chrono::nanoseconds sixty = engine::frame_period(60.0f);
    EXPECT_EQ(engine::frame_period(0.0f), sixty);
    EXPECT_EQ(engine::frame_period(-30.0f), sixty);
    EXPECT_EQ(engine::frame_period(std::numeric_limits<float>::quiet_NaN()), sixty);
    EXPECT_EQ(engine::frame_period(std::numeric_limits<float>::infinity()), sixty);
}

TEST(FramePacing, VsyncSwapIsTheOnlyWait) {
    EXPECT_EQ(engine::limiter_period(true, true, 0, 60.0f), std::nullopt);
    EXPECT_EQ(engine::limiter_period(true, true, 30, 60.0f), std::nullopt) << "the cap is ignored under vsync";
}

TEST(FramePacing, VsyncWithNoWindowToWaitOnSleepsToTheDisplay) {
    EXPECT_EQ(engine::limiter_period(true, false, 0, 144.0f), engine::frame_period(144.0f));
    EXPECT_EQ(engine::limiter_period(true, false, 240, 144.0f), engine::frame_period(144.0f));
    EXPECT_EQ(engine::limiter_period(true, false, 30, 144.0f), engine::frame_period(30.0f));
}

TEST(FramePacing, VsyncOffRunsAtTheCapOrUncapped) {
    EXPECT_EQ(engine::limiter_period(false, false, 0, 60.0f), std::nullopt);
    EXPECT_EQ(engine::limiter_period(false, false, 240, 60.0f), engine::frame_period(240.0f));
}

TEST(FrameLimiter, FirstFrameDoesNotWait) {
    engine::FrameLimiter limiter;
    EXPECT_EQ(limiter.wait_after(TimePoint{}, Ms{10}), std::chrono::nanoseconds::zero());
}

TEST(FrameLimiter, FastFramesWaitForTheRestOfThePeriod) {
    engine::FrameLimiter limiter;
    const engine::FrameLimiter::Clock::time_point start{};
    (void)limiter.wait_after(start, Ms{10});
    EXPECT_EQ(limiter.wait_after(start + Ms{1}, Ms{10}), Ms{9});
    // The previous frame slept until 10 ms; the next one is due at 20 ms.
    EXPECT_EQ(limiter.wait_after(start + Ms{12}, Ms{10}), Ms{8});
}

TEST(FrameLimiter, OversleptFrameKeepsTheSchedule) {
    engine::FrameLimiter limiter;
    const engine::FrameLimiter::Clock::time_point start{};
    (void)limiter.wait_after(start, Ms{10});
    // Due at 10 ms, woke at 11 ms: no wait, and the next frame is still due at 20 ms, not 21 ms.
    EXPECT_EQ(limiter.wait_after(start + Ms{11}, Ms{10}), std::chrono::nanoseconds::zero());
    EXPECT_EQ(limiter.wait_after(start + Ms{15}, Ms{10}), Ms{5});
}

TEST(FrameLimiter, StallStartsANewScheduleInsteadOfCatchingUp) {
    engine::FrameLimiter limiter;
    const engine::FrameLimiter::Clock::time_point start{};
    (void)limiter.wait_after(start, Ms{10});
    EXPECT_EQ(limiter.wait_after(start + Ms{100}, Ms{10}), std::chrono::nanoseconds::zero());
    EXPECT_EQ(limiter.wait_after(start + Ms{101}, Ms{10}), Ms{9});
}
