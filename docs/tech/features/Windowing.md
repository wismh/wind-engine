# Windowing

One process, any number of `ecs::World`, one loop. A world is one simulation. A window belongs to one world (`Worlds::bind_window`). Binding a window that another world already owns is fatal. `kPrimaryWindow` is id 0, the player's only window when the process has one. It is not the only render target.

## Contract

`IGame::primary_window()` returns a `WindowDesc`. `Engine::init` creates that window before `on_start`. Its id is `kPrimaryWindow` (`WindowId{0}`).

| `WindowDesc` field | Default |
| --- | --- |
| `title` | `"Game"` |
| `size` | `{800, 600}` |
| `position` | `nullopt` (platform placement) |
| `style` | all flags false except `resizable = true` |
| `owner` | `nullopt` (a top-level window of its own) |

`owner` makes the window a window of another open one: it stays above its owner, hides and minimizes with it, and has no taskbar entry of its own (Windows: the owner is set with `SDL_SetWindowParent`, `GWLP_HWNDPARENT`; the window is created hidden and shown once it has its owner, since Windows picks a taskbar entry when a window is shown). Create only. An owner that is not open makes `open_window` fail. The primary window cannot have one: `create_primary_window` refuses it (nothing is open before it). Where SDL cannot set an owner the window opens unowned (logged). Closing a window closes the windows it owns first (`WindowManager::destroy_window`): `SDL_DestroyWindow` destroys owned windows before their owner, and the manager must not keep one SDL already freed.

`WindowStyle`:

| Flag | When it applies |
| --- | --- |
| `borderless` | Create, and later `set_borderless` |
| `always_on_top` | Create, and later `set_always_on_top` |
| `transparent` | Create only. There is no setter |
| `resizable` | Create. `false` also omits the Windows maximize box |
| `maximized` | Create. Opens maximized with the title bar and taskbar still up. `size` is the restored size. SDL ignores this unless `resizable` is also set |
| `utility` | Create. A tool window (`SDL_WINDOW_UTILITY`): no taskbar entry, not in the window switcher; on Windows `WS_EX_TOOLWINDOW` and a smaller title bar. Pair it with `owner` so the window stays above the window it serves |

Further windows: `EngineServices::windows` is `IWindowControl`.

- `open_window` returns `nullopt` when it cannot create one (no primary yet, no video).
- `close_window` is mechanical. It closes the windows the closed one owns first.
- `raise` brings a window to the front and focuses it; a minimized one is restored first (`SDL_RestoreWindow`, `SDL_RaiseWindow`). Windows may refuse focus to a process that is not in the foreground and flash the taskbar entry instead.
- `open_windows` lists every live window, `kPrimaryWindow` included. The order is unspecified.
- `set_title` changes a window's title.
- `set_vsync`, `vsync`, `set_max_fps`, `max_fps` are process-wide frame pacing, not per window. See [Frame pacing](#frame-pacing).
- `set_position`, `resize`, `position`, `size` use screen coordinates. `position` and `size` are `nullopt` when that window is not open.
- `size()` is the client size in screen pixels. It is not the drawable size on `Presentation::sizes`.
- A call for an id that is not open is a no-op.
- `usable_display_bounds(display_index)` is the work area (display minus OS chrome). Out of range uses the primary display. A zero rect means the query failed.
- `usable_display_bounds_for_window` is the display that contains that window, or the primary display when the window is not open.

`Presentation::sizes` is the `WindowSizes` map `window_size_for` reads. `publish_size` writes `drawable_size()` into that map on attach for the primary window, on resize, and when `poll` backfills a secondary id that is still missing. `Host::write_window_size` writes the primary entry from the size it is given. `window_size_for` returns `{0,0}` when that id is absent. A world from `Worlds` whose presentation pointer was never set reports fatal through `ctx<IFatalError*>`. `bind_window` and `enable_ui` set it.

`ctx<ui::WindowSizes>()` is a different map. Resize, backfill, `Host::write_window_size`, and `window_size_for` do not use it.

`UiCanvas::window` selects which size and which pointer events that canvas uses. A canvas on another window does not see them.

World `Renderable`, `Sprite`, and `ParticleEmitter` draws go to every id in `ctx<BoundWindows>()`. The sorted list is shared. Each window's projection uses `window_size_for` (`Presentation.sizes`) on that window's command buffer. An empty list draws nothing. `EngineHost::attach_game` and the `Host` constructor bind `kPrimaryWindow`. The frame loop does not call `bind_window`. UI for a window is drawn by the world that owns it, after that world's clear and meshes.

## Events of a window

`SdlGlPresentation::dispatch` maps an SDL event's `windowID` to a `WindowId` with `event_window` (`src/core/event_window.h`): the live window with that SDL id (`WindowManager::find_by_sdl_id`). A non-zero id no live window has is dropped. Events are still queued for a window closed this frame (a dock float window opens and closes often), and they belong to no world now; they used to land on `kPrimaryWindow`, the game.

SDL id 0 means no window. By event kind (`NoWindowEvent`):

| Events | Id 0 |
| --- | --- |
| Resized, pixel size changed, close requested, focus lost | Dropped. `SDL_SendWindowEvent` always sends the window's own id |
| Key down and up, text editing, text input | `kPrimaryWindow`. SDL sends 0 while no window has keyboard focus; on a single-window platform (an Android key) that is the player's |
| Mouse button, motion, wheel | `kPrimaryWindow`. 0 while no window has mouse focus: on the web a release outside the canvas, which must still end the press |

Touch events carry no window and stay on the primary window's drawable size.

## Close

The OS close button sends `WindowCloseRequestedEvent`. The engine does not quit and does not destroy a game window because of it. The game reads the event. The one exception is a dock float window, whose close button docks its panels back ([Dock float windows](#dock-float-windows)).

## Dock float windows

A `DockSpace` with `DockFloatMode::OsWindow` opens one window per float through `EngineSystemDeps::windows` and binds it to its own world with `EngineSystemDeps::worlds` (`EngineHost` sets both to its `IWindowControl` and `Worlds`). Each is a `utility` window owned by the space's window. The dock layout system opens, moves, resizes, retitles, and closes them in `Bind`, follows a native move or resize into the layout, and reads the close button. A window it closes is unbound first. Closing in `Bind` is safe: `destroy_window` drops the window's command buffer with it, and no canvas of the frame still targets it. `Worlds::destroy` closes the float windows of the world it drops, the same way, before the world goes. See [Docking](Docking.md#os-window-floats).

## Open-file dialog

`request_open_file(owner, filters)` shows the platform open-file dialog (`SDL_ShowOpenFileDialog`), modal to `owner` where the platform supports it, and returns a `FileDialogCall` at once. `owner` is only the dialog's parent; no world is involved. `FileFilter` (`include/engine/core/file_dialog.h`) is a display name and a pattern: extensions without dots, separated by `;` (`"dll"`, `"png;jpg"`), or `*`. An empty list shows every file.

`request_open_folder(owner, start)` is the choose-folder dialog (`SDL_ShowOpenFolderDialog`) with the same call and the same answer: `path` is the chosen directory. It opens in `start` when that is an existing directory; otherwise the platform picks. The Wind Launcher uses it for a new project's location.

The caller owns the call, the same model as `HttpCall` ([Net](../modules/Net.md)). It is move-only. `pending()` is true until the answer is visible. `take()` returns the `FileDialogResult` once and empties the call. `path` is empty when the user cancelled or the dialog failed. `cancel()`, destroying the call, or move-assigning over it drops the answer. SDL cannot close the dialog from code, so it stays on screen until the user closes it. `FileDialogCall::resolved(result)` builds an answered call for fakes.

SDL may call back on another thread. The callback pushes the answer and the call's `FileDialogState` (`src/core/file_dialog_state.h`) into `FileDialogCompletions` under a mutex and touches nothing else. `poll` delivers after the SDL events of that frame, so every system of that frame sees the same state, and drops answers of cancelled calls. `FileDialogCompletions` and `HttpCompletions` are the same template, `CallCompletions` (`src/core/call_completions.h`). The queue and the state are shared with the SDL callback, so a dialog still open at shutdown, or after its call was dropped, does not write into freed memory.

## Editor windows

In the editor (`wind_editor`, see [Editor](Editor.md)) the editor window is a secondary window bound to the editor's world. `kPrimaryWindow` belongs to the game. Between plays no world is bound to it, so no system writes its command buffer and `draw_all` clears it to black and presents it. `EngineHost::detach_game` clears that buffer on Stop, because its `CmdDrawUI` entries point into the game world's documents. It also resets the window's NanoVG context (`EngineRuntime::reset_ui_cache`, `OpenGLCanvas::reset_ui_painter`), so fonts and images of the last game do not stay registered for the next build.

## Overlay and click-through

`OverlayMode` is engine-wide, not per window.

| Mode | Hooks |
| --- | --- |
| `Auto` (default) | On when any live window is `transparent` |
| `AlwaysEnabled` | On even before a transparent window exists |
| `AlwaysDisabled` | Off, including for an alpha-blended window |

The hooks are synthetic cursor polling for click-through, per-window transparent hit-test sync, and the Win32 modal-loop reentrant tick. Call `set_overlay_mode` before opening a transparent window if you need `AlwaysDisabled` from the first frame. `Auto` would turn the hooks on as soon as that window exists.

Click-through is a bounding box. `update_click_through` starts from whether `Presentation.mouse` contains that `WindowId`. A client cursor inside the drag region is then counted as a hit, before `should_be_click_through`. While `set_click_through_enabled` is on and the window is transparent, the window lets clicks through only when that hit is still false, so a cursor in the drag region stays on the window when `Presentation.mouse` does not contain the id. Hover updates that set, so it is not stuck on the last click. There is no per-pixel framebuffer test.

`reset_pointer_frame` clears `Presentation.mouse` at the start of `simulate_worlds`, before any world's `begin_frame`. `begin_frame` does not clear it. The hit-test inserts the window into `presentation_of(world).mouse`. `sync_frame` passes `worlds.presentation().mouse` to click-through, so it sees those hits. `world.ctx<ui::MouseConsumed>().consumed_for(window)` does not.

## Drag region

`set_drag_region` takes a rect in window-client pixels, the same space as `UiCanvas.rect`. `nullopt` clears it. The engine moves the window with `SDL_CaptureMouse`. It does not use the OS modal move loop.

A left click inside the rect starts a drag and is consumed before UI and ECS see it. A button inside that rect does not receive the click. Shrink the rect so it does not cover those controls.

## Frame pacing

`draw_all` (`WindowManager::draw_all`) ends each tick with at most one wait, so the loop runs at the display's refresh rate instead of as fast as it can.

| `IWindowControl` | Default | Effect |
| --- | --- | --- |
| `set_vsync(bool)` | on | On: the vsync window's swap waits for vblank. Off: every context has interval `0`, and only `max_fps` limits the loop |
| `set_max_fps(int)` | `0` (no cap) | Frames per second while no swap waits for vsync: vsync off, or on with no vsync window. Ignored while a vsync swap waits. A negative value is stored as `0` |

`WindowManager` holds both (`vsync()`, `max_fps()`); `WindowControlImpl` forwards to it. The next `draw_all` applies them. The editor keeps its own values across Play and puts them back on Stop ([Editor](Editor.md)).

Every window has its own GL context, and a swap interval belongs to a context. One window, the vsync window, swaps with vsync: adaptive (`SDL_GL_SetSwapInterval(-1)`, a late frame swaps at once instead of waiting another vblank) where the driver has it, else `1`. Every other window has interval `0`, set right after its context is created, whatever the driver's default. `draw_all` draws the other windows first and the vsync window last, so its swap is the tick's only wait. With two vsync windows the editor (game window plus editor window) would wait two vblanks per tick.

`choose_vsync_window` (`src/core/frame_pacing.h`) picks the vsync window every frame from the live windows:

1. Only a window that is presentable (not hidden, minimized, or occluded: a vsync swap there may return at once) and whose context accepted a swap interval.
2. `kPrimaryWindow` when it qualifies, else the lowest id that does.

When the choice changes, the old window's context goes back to `0` and the new one gets vsync. A context that refuses vsync is never chosen again.

With vsync off, no window is chosen. With no vsync window, `FrameLimiter` (`src/core/frame_limiter.h`) sleeps (`SDL_DelayPrecise`) after the draws for `limiter_period` (`src/core/frame_pacing.h`):

| Vsync | Vsync window | Period |
| --- | --- | --- |
| on | yes | none, the swap waited |
| on | none (every window minimized or hidden, a driver without swap control, no window) | the primary window's display refresh rate (`frame_period`, 60 Hz when unknown), or `max_fps` when that is lower |
| off | none | `max_fps`, or no sleep when it is `0` |

Frames keep a fixed schedule, so an oversleep does not lower the rate. A frame more than one period late starts a new schedule instead of running frames back to back.

The modal move and size loop's reentrant tick (`reentrant_tick`) calls `draw_all` too, so it is paced the same way.

Web does neither, and ignores `set_vsync` and `set_max_fps`: `GameLoop` runs on `requestAnimationFrame`, and SDL's Emscripten swap interval would change that main loop's timing.

A driver setting that forces vsync off still lets the swap return at once while the engine believes it has vsync. The engine does not detect that.

## Internals

Compiled only with `ENGINE_WITH_WINDOW`, under `src/render/opengl/`.

`WindowManager` owns one `WindowSystem`, one `OpenGLCanvas`, and one `CommandBuffer` per `WindowId`. Windows share the graphic factory and `AssetsDb`. Each canvas `draw` makes its GL context current before `execute`. A canvas also makes its own context current to delete its NanoVG painter and context, then puts back the context that was current: a VAO is not shared between contexts, so deleting the painter's VAO in another window's context deletes that window's VAO and its UI stops drawing. `destroy_window` of `kPrimaryWindow` drops the canvas before the SDL window for the same reason. `WindowManager` also owns the vsync window and the `FrameLimiter` ([Frame pacing](#frame-pacing)).

Public headers do not include SDL.

## Tests

`tests/window_style_test.cpp` and `tests/window_icon_test.cpp` do not call `SDL_Init(SDL_INIT_VIDEO)`. A live display is out of `engine_tests`. See [Boundaries](../architecture/Boundaries.md). `window_style_test` covers `utility` as `SDL_WINDOW_UTILITY`, `raise` of a window that is not open, and a primary window with an owner refused; an owned window on a display (no taskbar entry, closed with its owner) is not in `engine_tests`.

`tests/event_window_test.cpp` covers `event_window` without SDL: a live window's id, a closed window's id dropped for every kind, and id 0 per kind.

`tests/frame_pacing_test.cpp` covers the pure half of frame pacing: the primary window paces when it can, only one window paces, a hidden or minimized primary and a context without swap control hand it on, no candidate leaves it to the limiter; the period at 60 and 144 Hz and an unknown rate as 60 Hz; `limiter_period` for each row of the table above; the limiter's first frame, the rest of a period, an oversleep that keeps the schedule, and a stall that starts a new one. `tests/window_style_test.cpp` checks the defaults and the round trip through `IWindowControl` without a window. Whether a swap really waits needs a display and is not in `engine_tests`.

`tests/file_dialog_test.cpp` drives `FileDialogCall` and `FileDialogCompletions` without a dialog: an answer pushed from another thread is visible only after `deliver`, and taken once; a user cancel is an answer without a path; each call gets its own answer; `cancel`, a destroyed call, and a call replaced by move assignment drop the answer; `resolved` and an empty call.

## See also

- [Core](../modules/Core.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
- [Scope](../architecture/Scope.md)
