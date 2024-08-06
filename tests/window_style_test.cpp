#include <gtest/gtest.h>

#include <engine/igame.h>
#include <engine/ui/canvas.h>

#if defined(ENGINE_WITH_WINDOW)
#include "render/opengl/opengl_backend.h"
#include "render/opengl/window_control.h"
#include "render/opengl/window_manager.h"
#include "render/opengl/window_system.h"
#endif

namespace {

class DummyGame final : public engine::GameBase {};

TEST(WindowId, PrimaryWindowIsZero) {
    EXPECT_EQ(engine::kPrimaryWindow, engine::WindowId{0});
}

TEST(PrimaryWindow, DefaultsMatchSdd) {
    DummyGame game;
    const engine::WindowDesc desc = game.primary_window();
    EXPECT_EQ(desc.title, "Game");
    EXPECT_EQ(desc.size, (glm::ivec2{800, 600}));
    EXPECT_FALSE(desc.position.has_value());
    EXPECT_FALSE(desc.style.borderless);
    EXPECT_FALSE(desc.style.always_on_top);
    EXPECT_FALSE(desc.style.transparent);
}

#if defined(ENGINE_WITH_WINDOW)

TEST(WindowStyleFlags, DefaultStyleHasNoExtraFlags) {
    EXPECT_EQ(engine::window_style_flags(engine::WindowStyle{}), 0u);
}

TEST(WindowStyleFlags, BorderlessSetsBorderlessBit) {
    const engine::WindowStyle style{.borderless = true};
    EXPECT_EQ(engine::window_style_flags(style), SDL_WINDOW_BORDERLESS);
}

TEST(WindowStyleFlags, AlwaysOnTopSetsAlwaysOnTopBit) {
    const engine::WindowStyle style{.always_on_top = true};
    EXPECT_EQ(engine::window_style_flags(style), SDL_WINDOW_ALWAYS_ON_TOP);
}

TEST(WindowStyleFlags, TransparentSetsTransparentBit) {
    const engine::WindowStyle style{.transparent = true};
    EXPECT_EQ(engine::window_style_flags(style), SDL_WINDOW_TRANSPARENT);
}

TEST(WindowStyleFlags, FlagsCombine) {
    const engine::WindowStyle style{.borderless = true, .always_on_top = true, .transparent = true};
    const SDL_WindowFlags expected =
            SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_TRANSPARENT;
    EXPECT_EQ(engine::window_style_flags(style), expected);
}

TEST(WindowSystem, IsTransparentDefaultsToFalse) {
    // Verifying it flips to true after a real create() needs SDL_Init(SDL_INIT_VIDEO) and a
    // display/GPU, out of scope for engine_tests (SDD §12.3).
    engine::WindowSystem window;
    EXPECT_FALSE(window.is_transparent());
}

TEST(WindowSystem, RuntimeSettersAreNoopWithoutWindow) {
    engine::WindowSystem window;
    // No SDL_Init(SDL_INIT_VIDEO), no window created: must not crash (SDD §12.3 — no real
    // window in engine_tests).
    window.set_bordered(false);
    window.set_always_on_top(true);
    window.set_position({10, 20});
    window.resize({320, 240});
}

TEST(WindowControlImpl, DelegatesWithoutCrashingWithoutWindow) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager windows{backend};
    engine::WindowControlImpl control{windows};
    engine::IWindowControl& control_ref = control;
    control_ref.set_borderless(true);
    control_ref.set_always_on_top(true);
    control_ref.set_position({0, 0});
    control_ref.resize({100, 100});
    control_ref.set_click_through_enabled(true);
    control_ref.set_drag_region(engine::render::Rect{0, 0, 10, 10});
    control_ref.set_drag_region(std::nullopt);

    // No SDL video, no primary window ever created (SDD §12.3): open_window must fail (nullopt),
    // same no-primary contract WindowManager::create_window already guarantees on its own — the
    // point here is only that going through IWindowControl doesn't crash, not a specific outcome.
    EXPECT_FALSE(control_ref.open_window(engine::WindowDesc{}).has_value());
    control_ref.close_window(engine::WindowId{7});
    control_ref.close_window(engine::kPrimaryWindow);
}

TEST(ShouldBeClickThrough, TrueOnlyWhenEnabledTransparentAndNoHit) {
    EXPECT_TRUE(engine::should_be_click_through(true, true, false));
}

TEST(ShouldBeClickThrough, FalseWhenManuallyDisabled) {
    EXPECT_FALSE(engine::should_be_click_through(false, true, false));
}

TEST(ShouldBeClickThrough, FalseWhenWindowOpaque) {
    EXPECT_FALSE(engine::should_be_click_through(true, false, false));
}

TEST(ShouldBeClickThrough, FalseWhenPointerHitSomething) {
    EXPECT_FALSE(engine::should_be_click_through(true, true, true));
}

TEST(WindowSystem, ClickThroughEnabledDefaultsToFalse) {
    engine::WindowSystem window;
    EXPECT_FALSE(window.click_through_enabled());
}

TEST(WindowSystem, ClickThroughSetterAndUpdateAreNoopWithoutWindow) {
    engine::WindowSystem window;
    // No SDL_Init(SDL_INIT_VIDEO), no window created: must not crash (SDD §12.3).
    window.set_click_through_enabled(true);
    EXPECT_TRUE(window.click_through_enabled());
    window.update_click_through(false);
    window.update_click_through(true);
}

TEST(WindowSystem, SetDragRegionIsNoopWithoutWindow) {
    engine::WindowSystem window;
    // No SDL_Init(SDL_INIT_VIDEO), no window created: must not crash (SDD §12.3/§21.7).
    window.set_drag_region(engine::render::Rect{0, 0, 100, 32});
    window.set_drag_region(std::nullopt);
}

// WindowManager's success path (a real primary window + a secondary window sharing its GL
// context) needs a live display/GPU and is out of engine_tests scope (SDD §12.3), same boundary
// every prior phase here has respected — only the no-SDL-video failure-path bookkeeping is
// exercised below. Unlike WindowSystem's own create(), WindowManager::create_primary_window()
// is not exercised at all here (not even for a "does it fail" assertion): SDL3's SDL_CreateWindow
// auto-inits the video subsystem on demand, and on a machine with a real display attached (as
// opposed to a CI box with none) it picks the native video driver rather than the dummy/offscreen
// one this build also compiles in — actually calling create_primary_window() in this environment
// was observed to open a real, visible OS window (SDL_GL_CreateContext and all, ~250ms instead of
// the <1ms every other test here takes), which is exactly what §12.3 rules out.

TEST(WindowManager, CreateSecondaryWindowFailsWithoutPrimary) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager manager{backend};
    EXPECT_FALSE(manager.create_window(engine::WindowDesc{}).has_value());
}

TEST(WindowManager, FreshManagerReportsNoLiveWindows) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager manager{backend};
    const engine::WindowId other{7};

    EXPECT_FALSE(manager.has_window(engine::kPrimaryWindow));
    EXPECT_EQ(manager.window(engine::kPrimaryWindow), nullptr);
    EXPECT_EQ(manager.canvas(engine::kPrimaryWindow), nullptr);
    EXPECT_EQ(manager.commands(engine::kPrimaryWindow), nullptr);

    EXPECT_FALSE(manager.has_window(other));
    EXPECT_EQ(manager.window(other), nullptr);
    EXPECT_EQ(manager.canvas(other), nullptr);
    EXPECT_EQ(manager.commands(other), nullptr);
}

TEST(WindowManager, DestroyShutdownAndDrawAllAreNoopOnEmptyManager) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager manager{backend};
    // Must not crash: no SDL video, no window ever created (SDD §12.3).
    manager.destroy_window(engine::kPrimaryWindow);
    manager.destroy_window(engine::WindowId{7});
    manager.draw_all();
    manager.shutdown();
    manager.shutdown();
    EXPECT_FALSE(manager.has_window(engine::kPrimaryWindow));
}

TEST(WindowManager, FindBySdlIdReturnsNulloptWithNoLiveWindows) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager manager{backend};
    // No SDL video, no window ever created (SDD §12.3) — every slot (including the permanent
    // primary one) has no real SDL_Window, so no SDL_WindowID value can match.
    EXPECT_FALSE(manager.find_by_sdl_id(0).has_value());
    EXPECT_FALSE(manager.find_by_sdl_id(1).has_value());
    EXPECT_FALSE(manager.find_by_sdl_id(12345).has_value());
}

TEST(WindowManager, ForEachSecondaryWindowVisitsNothingOnFreshManager) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager manager{backend};
    // Fresh manager: the primary slot exists but has no live SDL window, and no secondary window
    // was ever created — the callback must never fire (SDD §21.7).
    int calls = 0;
    manager.for_each_secondary_window([&](engine::WindowId, engine::WindowSystem&) { ++calls; });
    EXPECT_EQ(calls, 0);
}

#endif

TEST(WindowCloseRequestedEvent, DefaultsToPrimaryWindow) {
    EXPECT_EQ(engine::ui::WindowCloseRequestedEvent{}.window, engine::kPrimaryWindow);
}

}
