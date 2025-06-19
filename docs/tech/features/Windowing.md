---
tags: [feature]
---

# Windowing

One `ecs::World` and one loop; many OS windows. A window is an output target (`WindowId`), not a second simulation.

## Contract

- `IGame::primary_window()` returns a `WindowDesc` (title, size, optional position, `WindowStyle`). That window is `kPrimaryWindow` and must exist before `on_start`.
- Further windows: `IWindowControl::open_window` / `close_window` (DI, not a service locator). Failure (no primary yet, no video) returns `nullopt`.
- `WindowStyle::{borderless, always_on_top, transparent, resizable}`. Transparent cannot be toggled after create; open another window instead.
- World `Renderable`s draw only into `kPrimaryWindow`. Secondary windows are UI-only (`UiCanvas::window`).
- Drawable size: `ctx<ui::WindowSizes>()` keyed by `WindowId`. `window_size_for(world, id)` returns `{0,0}` until the first resize/backfill.
- `UiCanvas::window` selects which window sizes and hit-tests that canvas. Pointer events carry `window`; a canvas on another window never receives them.
- Close button: `WindowCloseRequestedEvent` only. The engine never quits or destroys a window on its own.
- Platform SDL / Win32 calls stay in `src/render/opengl/` (`WindowManager`, `WindowSystem`). Public headers stay GL/SDL-free.

## Overlay and click-through

`OverlayMode` (`Auto` / `AlwaysEnabled` / `AlwaysDisabled`) is engine-wide. Auto turns overlay hooks on when any live window is transparent.

Click-through (transparent windows) is **bounding-box**: the window is click-through unless that window's `MouseConsumed` is set (widget hit). Hover updates `MouseConsumed` so it is not stuck at the last click. Per-pixel framebuffer sampling is out of scope ([[architecture/Scope]]).

`set_drag_region` marks a client-pixel rect as a titlebar for borderless windows. A left click inside it starts an engine-owned drag (`SDL_CaptureMouse`) and never reaches UI/ECS. Shrink the rect so interactive controls are not inside it.

## Internals (windowed)

`WindowManager` owns one `WindowSystem` + `OpenGLCanvas` + `CommandBuffer` pair per `WindowId`. Windows share `IGraphicFactory` / `AssetsDb`. Each `OpenGLCanvas::draw()` makes its GL context current and re-arms the shared backend's NanoVG painter before `execute()`.

## Tests

Logic without `SDL_Init(SDL_INIT_VIDEO)`: [[tests.window_style_test.cpp]], [[tests.window_icon_test.cpp]]. Real pixels and a live display are out of `engine_tests` ([[architecture/Boundaries]]).

## See also

- [[include.engine.core.window_desc.h]]
- [[include.engine.core.window_control.h]]
- [[include.engine.core.engine_runtime.h]]
- [[include.engine.ui.canvas.h]]
- [[modules/Core]]
