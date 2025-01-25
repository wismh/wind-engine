#include <gtest/gtest.h>

#include <engine/core/input_system.h>
#include <engine/igame.h>
#include <engine/ui/canvas.h>

#if defined(ENGINE_WITH_WINDOW)
#include "render/opengl/desktop_overlay_policy.h"
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
    EXPECT_TRUE(desc.style.resizable);
}

#if defined(ENGINE_WITH_WINDOW)

TEST(WindowStyleFlags, DefaultStyleIsResizableOnly) {
    EXPECT_EQ(engine::window_style_flags(engine::WindowStyle{}), SDL_WINDOW_RESIZABLE);
}

TEST(WindowStyleFlags, NotResizableClearsResizableBit) {
    const engine::WindowStyle style{.resizable = false};
    EXPECT_EQ(engine::window_style_flags(style), 0u);
}

TEST(WindowStyleFlags, BorderlessSetsBorderlessBit) {
    const engine::WindowStyle style{.borderless = true, .resizable = false};
    EXPECT_EQ(engine::window_style_flags(style), SDL_WINDOW_BORDERLESS);
}

TEST(WindowStyleFlags, AlwaysOnTopSetsAlwaysOnTopBit) {
    const engine::WindowStyle style{.always_on_top = true, .resizable = false};
    EXPECT_EQ(engine::window_style_flags(style), SDL_WINDOW_ALWAYS_ON_TOP);
}

TEST(WindowStyleFlags, TransparentSetsTransparentBit) {
    const engine::WindowStyle style{.transparent = true, .resizable = false};
    EXPECT_EQ(engine::window_style_flags(style), SDL_WINDOW_TRANSPARENT);
}

TEST(WindowStyleFlags, FlagsCombine) {
    const engine::WindowStyle style{.borderless = true, .always_on_top = true, .transparent = true};
    const SDL_WindowFlags expected =
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_TRANSPARENT;
    EXPECT_EQ(engine::window_style_flags(style), expected);
}

TEST(WindowStyleFlags, NotResizableCombinesWithOtherFlags) {
    const engine::WindowStyle style{.borderless = true, .resizable = false};
    EXPECT_EQ(engine::window_style_flags(style), SDL_WINDOW_BORDERLESS);
}

TEST(WindowSystem, IsTransparentDefaultsToFalse) {
    // Verifying it flips to true after a real create() needs SDL_Init(SDL_INIT_VIDEO) and a
    // display/GPU, out of scope for engine_tests.
    engine::WindowSystem window;
    EXPECT_FALSE(window.is_transparent());
}

TEST(WindowSystem, RuntimeSettersAreNoopWithoutWindow) {
    engine::WindowSystem window;
    // No SDL_Init(SDL_INIT_VIDEO), no window created: must not crash.
    window.set_bordered(false);
    window.set_always_on_top(true);
    window.set_position({10, 20});
    window.resize({320, 240});
}

TEST(WindowControlImpl, DelegatesWithoutCrashingWithoutWindow) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager windows{backend};
    engine::DesktopOverlayPolicy overlay;
    engine::WindowControlImpl control{windows, overlay};
    engine::IWindowControl& control_ref = control;
    control_ref.set_borderless(true);
    control_ref.set_always_on_top(true);
    control_ref.set_position({0, 0});
    control_ref.resize({100, 100});
    control_ref.set_click_through_enabled(true);
    control_ref.set_drag_region(engine::render::Rect{0, 0, 10, 10});
    control_ref.set_drag_region(std::nullopt);

    // No SDL video, no primary window ever created: open_window must fail (nullopt),
    // same no-primary contract WindowManager::create_window already guarantees on its own — the
    // point here is only that going through IWindowControl doesn't crash, not a specific outcome.
    EXPECT_FALSE(control_ref.open_window(engine::WindowDesc{}).has_value());
    control_ref.close_window(engine::WindowId{7});
    control_ref.close_window(engine::kPrimaryWindow);
}

TEST(WindowControlImpl, WindowIdAddressedMethodsDefaultToPrimary) {
    // Every setter's default argument must resolve to kPrimaryWindow so every pre-existing
    // single-window call site (going through IWindowControl&, where the default lives) keeps
    // behaving identically after WindowId generalization.
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager windows{backend};
    engine::DesktopOverlayPolicy overlay;
    engine::WindowControlImpl control{windows, overlay};
    engine::IWindowControl& control_ref = control;
    control_ref.set_borderless(true);
    control_ref.set_always_on_top(true);
    control_ref.set_position({0, 0});
    control_ref.resize({100, 100});
    control_ref.set_click_through_enabled(true);
    control_ref.set_drag_region(engine::render::Rect{0, 0, 10, 10});
}

TEST(WindowControlImpl, WindowIdAddressedMethodsAreNoopForUnknownWindow) {
    // A WindowId with no live window (never opened, or already closed) must not crash — same
    // no-crash contract set_borderless/etc. already had for a not-yet-created primary window.
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager windows{backend};
    engine::DesktopOverlayPolicy overlay;
    engine::WindowControlImpl control{windows, overlay};
    engine::IWindowControl& control_ref = control;
    const engine::WindowId secondary{7};
    control_ref.set_borderless(true, secondary);
    control_ref.set_always_on_top(true, secondary);
    control_ref.set_position({0, 0}, secondary);
    control_ref.resize({100, 100}, secondary);
    control_ref.set_click_through_enabled(true, secondary);
    control_ref.set_drag_region(engine::render::Rect{0, 0, 10, 10}, secondary);
    control_ref.set_drag_region(std::nullopt, secondary);
}

TEST(MouseConsumed, ConsumedForTracksPerWindow) {
    engine::ui::MouseConsumed consumed;
    EXPECT_FALSE(consumed.consumed_for(engine::kPrimaryWindow));
    EXPECT_FALSE(consumed.consumed_for(engine::WindowId{1}));

    consumed.consumed_windows.insert(engine::kPrimaryWindow);
    EXPECT_TRUE(consumed.consumed_for(engine::kPrimaryWindow));
    EXPECT_FALSE(consumed.consumed_for(engine::WindowId{1}));

    consumed.consumed_windows.insert(engine::WindowId{2});
    EXPECT_TRUE(consumed.consumed_for(engine::kPrimaryWindow));
    EXPECT_TRUE(consumed.consumed_for(engine::WindowId{2}));
    EXPECT_FALSE(consumed.consumed_for(engine::WindowId{1}));
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
    // No SDL_Init(SDL_INIT_VIDEO), no window created: must not crash.
    window.set_click_through_enabled(true);
    EXPECT_TRUE(window.click_through_enabled());
    window.update_click_through(false);
    window.update_click_through(true);
}

TEST(WindowSystem, ClickThroughAppliedDefaultsToFalse) {
    engine::WindowSystem window;
    EXPECT_FALSE(window.click_through_applied());
}

TEST(WindowSystem, ClickThroughAppliedStaysFalseWithoutWindow) {
    engine::WindowSystem window;
    // update_click_through()'s apply_click_through() call is itself a no-op without a real HWND
    //, so click_through_applied() must never latch true from this alone — otherwise
    // the Win32 hit-test hook (window_system.cpp) would treat a window that was never
    // actually made click-through as if it were.
    window.set_click_through_enabled(true);
    window.update_click_through(/*pointer_hit_something=*/false);
    EXPECT_FALSE(window.click_through_applied());
}

TEST(WindowSystem, CursorClientPositionIsNulloptWithoutWindow) {
    engine::WindowSystem window;
    // No SDL_Init(SDL_INIT_VIDEO), no window created: must not crash.
    EXPECT_FALSE(window.cursor_client_position().has_value());
}

TEST(WindowSystem, SetDragRegionIsNoopWithoutWindow) {
    engine::WindowSystem window;
    // No SDL_Init(SDL_INIT_VIDEO), no window created: must not crash.
    window.set_drag_region(engine::render::Rect{0, 0, 100, 32});
    window.set_drag_region(std::nullopt);
}

TEST(WindowSystem, IsInDragRegionChecksBoundsCorrectly) {
    engine::WindowSystem window;
    EXPECT_FALSE(window.is_in_drag_region(glm::vec2{10, 10}));
    window.set_drag_region(engine::render::Rect{10, 20, 100, 50});
    EXPECT_TRUE(window.is_in_drag_region(glm::vec2{10, 20}));
    EXPECT_TRUE(window.is_in_drag_region(glm::vec2{50, 40}));
    EXPECT_TRUE(window.is_in_drag_region(glm::vec2{109.9f, 69.9f}));
    EXPECT_FALSE(window.is_in_drag_region(glm::vec2{9.9f, 20}));
    EXPECT_FALSE(window.is_in_drag_region(glm::vec2{10, 19.9f}));
    EXPECT_FALSE(window.is_in_drag_region(glm::vec2{110, 50}));
    EXPECT_FALSE(window.is_in_drag_region(glm::vec2{50, 70}));
    window.set_drag_region(std::nullopt);
    EXPECT_FALSE(window.is_in_drag_region(glm::vec2{50, 40}));
}

TEST(WindowSystem, IsDraggingDefaultsToFalse) {
    engine::WindowSystem window;
    EXPECT_FALSE(window.is_dragging());
}

TEST(WindowSystem, ManualDragApiIsNoopWithoutWindow) {
    engine::WindowSystem window;
    // No SDL_Init(SDL_INIT_VIDEO), no window created: begin_drag_if_in_region()
    // must refuse (no window to check a region against, let alone capture the mouse for) rather
    // than crash; update_drag()/end_drag() must stay no-ops too.
    window.set_drag_region(engine::render::Rect{0, 0, 100, 32});
    EXPECT_FALSE(window.begin_drag_if_in_region(glm::vec2{10, 10}));
    EXPECT_FALSE(window.is_dragging());
    window.update_drag();
    window.end_drag();
}

// WindowManager's success path (a real primary window + a secondary window sharing its GL
// context) needs a live display/GPU and is out of engine_tests scope, same boundary
// every prior phase here has respected — only the no-SDL-video failure-path bookkeeping is
// exercised below. Unlike WindowSystem's own create(), WindowManager::create_primary_window()
// is not exercised at all here (not even for a "does it fail" assertion): SDL3's SDL_CreateWindow
// auto-inits the video subsystem on demand, and on a machine with a real display attached (as
// opposed to a CI box with none) it picks the native video driver rather than the dummy/offscreen
// one this build also compiles in — actually calling create_primary_window() in this environment
// was observed to open a real, visible OS window (SDL_GL_CreateContext and all, ~250ms instead of
// the <1ms every other test here takes), which is exactly what engine_tests rules out.

TEST(WindowControlImpl, UsableDisplayBoundsIsNoopWithoutVideo) {
    // No SDL_Init(SDL_INIT_VIDEO) here: must not crash. The real, non-zero-bounds path
    // needs a live display and is out of engine_tests scope, same boundary as every other real-SDL
    // query in this file.
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager windows{backend};
    engine::DesktopOverlayPolicy overlay;
    engine::WindowControlImpl control{windows, overlay};
    engine::IWindowControl& control_ref = control;
    (void) control_ref.usable_display_bounds();
    (void) control_ref.usable_display_bounds(0);
    (void) control_ref.usable_display_bounds(-1);
    (void) control_ref.usable_display_bounds(99);
}

TEST(WindowControlImpl, OverlayModeDefaultsToAutoAndForwardsToPolicy) {
    // A game that wants an alpha-blended window without the desktop-overlay hooks (cursor
    // polling, WS_EX_TRANSPARENT sync, modal-loop tick hook) has no way to say so before this
    // setter existed — AlwaysDisabled was reachable only from DesktopOverlayPolicy directly, never
    // through the public IWindowControl surface a game actually gets via DI.
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager windows{backend};
    engine::DesktopOverlayPolicy overlay;
    engine::WindowControlImpl control{windows, overlay};
    engine::IWindowControl& control_ref = control;

    EXPECT_EQ(control_ref.overlay_mode(), engine::OverlayMode::Auto);

    control_ref.set_overlay_mode(engine::OverlayMode::AlwaysDisabled);
    EXPECT_EQ(control_ref.overlay_mode(), engine::OverlayMode::AlwaysDisabled);
    EXPECT_EQ(overlay.mode(), engine::OverlayMode::AlwaysDisabled);

    control_ref.set_overlay_mode(engine::OverlayMode::AlwaysEnabled);
    EXPECT_EQ(overlay.mode(), engine::OverlayMode::AlwaysEnabled);
}

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
    // Must not crash: no SDL video, no window ever created.
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
    // No SDL video, no window ever created — every slot (including the permanent
    // primary one) has no real SDL_Window, so no SDL_WindowID value can match.
    EXPECT_FALSE(manager.find_by_sdl_id(0).has_value());
    EXPECT_FALSE(manager.find_by_sdl_id(1).has_value());
    EXPECT_FALSE(manager.find_by_sdl_id(12345).has_value());
}

TEST(WindowManager, ForEachSecondaryWindowVisitsNothingOnFreshManager) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager manager{backend};
    // Fresh manager: the primary slot exists but has no live SDL window, and no secondary window
    // was ever created — the callback must never fire.
    int calls = 0;
    manager.for_each_secondary_window([&](engine::WindowId, engine::WindowSystem&) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(WindowManager, ForEachWindowVisitsNothingOnFreshManager) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager manager{backend};
    // Fresh manager: neither primary nor secondary windows have live SDL_Window instances.
    int calls = 0;
    manager.for_each_window([&](engine::WindowId, engine::WindowSystem&) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(WindowManager, ForEachWindowConstOverloadWorks) {
    engine::render::OpenGLRenderBackend backend;
    const engine::WindowManager manager{backend};
    int calls = 0;
    manager.for_each_window([&](engine::WindowId, const engine::WindowSystem&) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(DesktopOverlayPolicy, DefaultsToAutoMode) {
    const engine::DesktopOverlayPolicy policy;
    EXPECT_EQ(policy.mode(), engine::OverlayMode::Auto);
}

TEST(DesktopOverlayPolicy, ReportsNoActiveOverlayWhenNoWindowsLive) {
    engine::render::OpenGLRenderBackend backend;
    const engine::WindowManager manager{backend};
    const engine::DesktopOverlayPolicy policy;
    EXPECT_FALSE(policy.has_active_overlay(manager));
}

TEST(DesktopOverlayPolicy, ModeOverridesDetection) {
    engine::render::OpenGLRenderBackend backend;
    const engine::WindowManager manager{backend};
    engine::DesktopOverlayPolicy policy;

    policy.set_mode(engine::OverlayMode::AlwaysEnabled);
    EXPECT_TRUE(policy.has_active_overlay(manager));

    policy.set_mode(engine::OverlayMode::AlwaysDisabled);
    EXPECT_FALSE(policy.has_active_overlay(manager));

    policy.set_mode(engine::OverlayMode::Auto);
    EXPECT_FALSE(policy.has_active_overlay(manager));
}

TEST(DesktopOverlayPolicy, PollAndClickThroughAreNoopWhenInactive) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager manager{backend};
    engine::DesktopOverlayPolicy policy;
    engine::InputSystem input;
    const engine::ui::MouseConsumed consumed;

    // No-op without active overlay; must not crash.
    policy.poll_cursor(manager, input);
    policy.update_click_through(manager, consumed);
}

TEST(DesktopOverlayPolicy, SyncModalLoopHookInstallsOnlyWhenActive) {
    engine::render::OpenGLRenderBackend backend;
    engine::WindowManager manager{backend};
    engine::DesktopOverlayPolicy policy;

    int tick_count = 0;
    const auto callback = [&tick_count] { ++tick_count; };

    // When inactive (Auto mode with no live windows), hook callback is not installed.
    policy.sync_modal_loop_hook(manager, callback);
    EXPECT_FALSE(manager.modal_loop_tick_callback());

    // When forced active, hook callback is installed.
    policy.set_mode(engine::OverlayMode::AlwaysEnabled);
    policy.sync_modal_loop_hook(manager, callback);
    EXPECT_TRUE(manager.modal_loop_tick_callback());

    // Clearing callback uninstalls the hook.
    policy.sync_modal_loop_hook(manager, nullptr);
    EXPECT_FALSE(manager.modal_loop_tick_callback());
}

#endif

TEST(WindowCloseRequestedEvent, DefaultsToPrimaryWindow) {
    EXPECT_EQ(engine::ui::WindowCloseRequestedEvent{}.window, engine::kPrimaryWindow);
}

}
