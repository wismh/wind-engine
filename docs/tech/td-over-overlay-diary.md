# Desktop Overlay & td-over Integration Diary (wind-88 – wind-94)

> **Note:** This document archives the detailed development log, empirical findings, diagnostic iterations,
> and game-specific reports from `td-over` (an idle auto-battler companion game running as a desktop overlay)
> during the implementation and stabilization of Wind's multi-window desktop overlay support.
> It has been preserved separately to keep `docs/sdd.md` focused on current architectural specifications.

---

## 1. Initial Integration & Windowing Reports

### Transparency Black-Screen & `on_update` Root Cause (§12.2 / §21.2)
`td-over` originally reported a transparent primary window rendering opaque black on Windows. Investigating that report live (running the game, `GetWindowLongPtr` inspection, temporary diagnostic logging) found two unrelated issues before transparency itself could be exercised:
1. Borderless-titlebar style needed `SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "0")`.
2. The downstream game's own `on_update()` override never called `world_.run(Schedule::Fixed/Frame)` (or `GameBase::on_update()`), so every engine-registered system (`Phase::Render`/`UiRender` included) silently never ran for either window.

### Double-Click Maximize Bug (§21.3 / §21.7)
`td-over` reported a double-click inside a `set_drag_region()` region maximizing its fixed-size overlay window.
*Root cause:* `WindowSystem::create()` used to pass `SDL_WINDOW_RESIZABLE` unconditionally. Fixed by splitting out `WindowStyle::resizable` (default true) and clearing `SDL_WINDOW_RESIZABLE` for fixed-size overlays.

### Click-Through on Inactive Windows (§21.3 / §21.6)
Clicking a `Button` in any window that was not the OS-focused window did nothing on the first click; only the second click worked.
*Root cause:* SDL3's default behavior swallows the activating click. Fixed by setting `SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1")` in `EngineRuntime::init_video()`.

### Cross-Window UI State Leak (§21.6)
Hover and pressed visuals leaked across windows: hovering a button on a secondary window painted button 0 on the primary window as hovered.
*Root cause:* `UiInputSystem` hit-testing stored a flat `element_id` without window disambiguation. Fixed by scoping hovered/pressed state per `WindowId`.

---

## 2. Click-Through & Drag Regions (wind-88)

### Drag Region Silently Breaking Click-Through
Once a window installed an `SDL_HitTest` callback via `SDL_SetWindowHitTest`, `WIN_WindowProc` in SDL3 mapped `SDL_HITTEST_NORMAL` directly to `HTCLIENT` without falling through to `DefWindowProc`. Because `DefWindowProc` is the only place in Windows that inspects `WS_EX_TRANSPARENT` to return `HTTRANSPARENT`, installing any hit test callback broke OS-level click-through everywhere across the window.
*Fix:* Subclassed the HWND with `win32_hit_test_wndproc` to intercept `WM_NCHITTEST` and return `HTTRANSPARENT` when click-through is active and the point falls outside the drag region.

### The Click-Through Motion Deadlock
Once `WS_EX_TRANSPARENT` took effect, Windows stopped delivering `WM_MOUSEMOVE` messages for any point resolving to `HTTRANSPARENT`. Because `MouseConsumed` only updated in response to real `SDL_EVENT_MOUSE_MOTION` events, moving off a button onto empty space latched `MouseConsumed = false`, permanently answering `HTTRANSPARENT` even when the cursor returned over buttons.
*Fix:* Added `WindowSystem::cursor_client_position()` polling `SDL_GetGlobalMouseState` minus `SDL_GetWindowPosition` every tick when click-through is enabled, synthesizing motion events.

### The 3-Round Diagnostic Iteration (`samples/overlay_probe`)
Diagnosed using `samples/overlay_probe` and `%TEMP%\wind88_diagnostic.log`:
- **Round 1:** `SendMessage(hwnd, WM_NCHITTEST, ...)` answered `HTTRANSPARENT`, but real clicks still did not reach Explorer underneath. On DWM-composited windows, `WS_EX_TRANSPARENT` alone is insufficient without `WS_EX_LAYERED`.
- **Round 2:** Adding bare `WS_EX_LAYERED` enabled click-through, but broke the drag region and keyboard focus. Diagnostic logs showed 0 `HTCAPTION` hits. Root cause: `WS_EX_LAYERED` without calling `SetLayeredWindowAttributes` leaves blend state undefined, making Windows treat the entire window as input-transparent. Fixed by arming it with `SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)`.
- **Round 3:** With `WS_EX_LAYERED` armed, the drag region still failed despite answering `HTCAPTION` 698 times. Real click delivery on layered+transparent composited windows depends on whether `WS_EX_TRANSPARENT` is set *at the instant the click lands*. Fixed by excluding `drag_region_` from `update_click_through()`.

---

## 3. Modal Drag Loop & Simulation Freezing (wind-89 – wind-92)

### Reentrant Game Tick (wind-89)
When dragging a window on Windows, `DefWindowProc` enters a modal move/size loop, blocking `EngineRuntime::tick_loop()`. `WindowManager` installed a message hook via `SDL_SetWindowsMessageHook` to receive `WM_TIMER` (~10ms).
At `td-over`'s request (an idle game requiring simulation to advance while dragging), the visual-only redraw was replaced with `EngineRuntime::reentrant_tick()`, advancing `FixedStepClock` without calling `world.flush_events()` (to avoid discarding events before outer systems run) or `poll_events()`.

### The Opaque Secondary Drag Stall (wind-90 & wind-91)
- **wind-90:** Dragging the transparent primary overlay was smooth, but dragging opaque secondary windows (`workshop`/`settings`) caused stalls. `td-over` hypothesized that `SDL_GL_SwapWindow` was blocking on DWM for the dragged window. A selective draw skip was introduced: `draw_all(skip)` skipped drawing the dragged window.
- **wind-91:** The wind-90 skip regressed the primary overlay, which froze visually when dragged. Fixed by restricting the draw skip strictly to non-transparent windows.

### Root Cause Discovery & Multimedia Timer Proof (wind-92)
`td-over` instrumented `canvas->draw()` with `%TEMP%\wind_swap_timing.log`:
- `canvas->draw()` never exceeded ~12ms, disproving the `SwapWindow` blocking hypothesis.
- The real issue was `WM_TIMER` delivery in the window's message queue: while dragging an opaque secondary window, gaps between timer messages reached 31–547ms.
- A multimedia timer (`timeSetEvent`) running on a separate thread maintained a rock-solid ~16ms cadence through the exact spans where `WM_TIMER` stalled.
- *Root cause:* DWM synchronously captures thumbnail/move representations for opaque windows during modal moves, starving the window message queue.
- *Solution:* Rather than multi-threading the entire engine ECS and GL context, `WindowSystem::begin_drag_if_in_region()` implemented custom window dragging via `SDL_CaptureMouse(true)`, tracking cursor delta with `SDL_SetWindowPosition`. This avoided entering `DefWindowProc`'s modal move loop entirely for drag regions.

---

## 4. Architectural Cleanup & Policy Isolation (wind-93 & wind-94)

### Cleanup & Symmetry (wind-93)
- Removed `skip_draw`, `dragged_window`, and `find_by_native_handle`.
- Replaced friend C-callback `window_drag_hit_test` with `WindowSystem::is_in_drag_region()`.
- Extended click-through and drag support symmetrically to secondary windows.

### Policy Isolation (wind-94)
- Isolated cursor polling, click-through updates, and Win32 hook registration into `DesktopOverlayPolicy`.
- Uninstalled `SDL_SetWindowsMessageHook` by default; registered dynamically only when an active overlay is present.
- Returned the core engine loop to a clean, deterministic loop for standard games.
