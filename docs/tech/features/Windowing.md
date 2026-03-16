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

`WindowStyle`:

| Flag | When it applies |
| --- | --- |
| `borderless` | Create, and later `set_borderless` |
| `always_on_top` | Create, and later `set_always_on_top` |
| `transparent` | Create only. There is no setter |
| `resizable` | Create. `false` also omits the Windows maximize box |
| `maximized` | Create. Opens maximized with the title bar and taskbar still up. `size` is the restored size. SDL ignores this unless `resizable` is also set |

Further windows: `EngineServices::windows` is `IWindowControl`.

- `open_window` returns `nullopt` when it cannot create one (no primary yet, no video).
- `close_window` is mechanical.
- `set_position`, `resize`, `position`, `size` use screen coordinates. `position` and `size` are `nullopt` when that window is not open.
- `size()` is the client size in screen pixels. It is not the drawable size on `Presentation::sizes`.
- A call for an id that is not open is a no-op.
- `usable_display_bounds(display_index)` is the work area (display minus OS chrome). Out of range uses the primary display. A zero rect means the query failed.
- `usable_display_bounds_for_window` is the display that contains that window, or the primary display when the window is not open.

`Presentation::sizes` is the `WindowSizes` map `window_size_for` reads. `publish_size` writes `drawable_size()` into that map on attach for the primary window, on resize, and when `poll` backfills a secondary id that is still missing. `Host::write_window_size` writes the primary entry from the size it is given. `window_size_for` returns `{0,0}` when that id is absent. A world from `Worlds` whose presentation pointer was never set reports fatal through `ctx<IFatalError*>`. `bind_window` and `enable_ui` set it.

`ctx<ui::WindowSizes>()` is a different map. Resize, backfill, `Host::write_window_size`, and `window_size_for` do not use it.

`UiCanvas::window` selects which size and which pointer events that canvas uses. A canvas on another window does not see them.

World `Renderable`, `Sprite`, and `ParticleEmitter` draws go to every id in `ctx<BoundWindows>()`. The sorted list is shared. Each window's projection uses `window_size_for` (`Presentation.sizes`) on that window's command buffer. An empty list draws nothing. `Engine::init` and the `Host` constructor bind `kPrimaryWindow`. The frame loop does not call `bind_window`. UI for a window is drawn by the world that owns it, after that world's clear and meshes.

## Close

The OS close button sends `WindowCloseRequestedEvent`. The engine does not quit and does not destroy a game window because of it. The game reads the event.

The inspector window and the profiler window are the exception: a close request for that tool window turns the tool off, and the engine closes it. The event is still delivered. The primary window is not closed that way. See [UI Inspector](UI%20Inspector.md) and [UI Profiler](UI%20Profiler.md).

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

## Internals

Compiled only with `ENGINE_WITH_WINDOW`, under `src/render/opengl/`.

`WindowManager` owns one `WindowSystem`, one `OpenGLCanvas`, and one `CommandBuffer` per `WindowId`. Windows share the graphic factory and `AssetsDb`. Each canvas `draw` makes its GL context current before `execute`.

Public headers do not include SDL.

## Tests

`tests/window_style_test.cpp` and `tests/window_icon_test.cpp` do not call `SDL_Init(SDL_INIT_VIDEO)`. A live display is out of `engine_tests`. See [Boundaries](../architecture/Boundaries.md).

## See also

- [Core](../modules/Core.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
- [Scope](../architecture/Scope.md)
