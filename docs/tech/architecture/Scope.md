# Scope

Wind is a 2D C++ game engine for production titles. Development happens in the Wind editor. The editor is the host process, and a game is a guest module that the editor loads on Play and unloads on Stop. An exported game is a standalone executable that links the engine statically. Games do not vendor SDL or copy engine sources.

The editor is in progress. See [Editor Plan](Editor%20Plan.md). Today `wind_editor` chooses a game module, plays it, inspects and profiles its UI in its Inspector and Profiler tabs, and stops it ([Editor](../features/Editor.md)).

## In scope

- Lifecycle: init, main loop, shutdown.
- The editor: a host executable that owns the engine services, its own window, and the game's `kPrimaryWindow`. It loads a game module on Play and unloads it on Stop. The engine is a shared library only in the editor build. Exported builds stay static.
- Editor tools. UI Inspector and UI Profiler live in the editor window only. The engine keeps the data they read. A game does not open them.
- Window, GL context, input actions, mouse to UI. Multiple windows for one running game. See [Windowing](../features/Windowing.md).
- A process holds any number of `ecs::World`. A world is one simulation: no parent and no scene graph. A window belongs to one world. That world's meshes go to each window bound to it. UI is `UiCanvas` plus a document instance plus a view-model. The document comes from XML or the C++ builder.
- Homemade ECS. EnTT is an API reference only, not a dependency.
- Render abstractions and an OpenGL 3.3 backend: materials, instance tint, layer sort. NanoVG executes `CmdDrawUI`. Particles are `CmdDrawParticles`.
- `AssetsDb` and a GUID catalog: sidecar TOML `.meta`, `asset_guid`, and `asset_codegen`.
- Localization: `.strings` tables by `AssetId`, `{tr}` in UI, integer plural categories. The game selects the active locale and persists that choice.
- Audio: buses, SFX pool, music A/B, looping SFX handles on SDL3_mixer.
- Double-buffered event queues, not a callback bus.
- `Engine::init` passes engine services to the game constructor as `EngineServices`.
- Per-user writable directory (`user_data_directory`) for saves and settings. The game owns the file format and when to write.
- HTTP and HTTPS requests (`IHttpClient`) on Windows, Android, and Web. The caller owns each request as an `HttpCall`.
- GoogleTest suite for engine logic (`engine_tests`).

## Backlog

Open engine work, not game concerns:

- Editor: project list, launching the game build, and the remaining tools. The first version picks a game module by file on every start.
- Packed asset bundles (still GUID-addressed).
- A separate cue `Sound` that references a clip GUID. Today one file is one cue.
- `Transform` parent and a world-matrix chain.
- CSS `@import`. WPF `IValueConverter` and `Mode=TwoWay` as a general converter. Checkbox `checked` already writes a `Bindable` back.
- Widget-as-ECS-entity. That would replace the XML instance tree inside `UiCanvas`.
- Input: SDL gamepad poll and bind (`ControlKind::GamepadButton` and `GamepadAxis` exist; events are not wired), WASD composites, action maps.
- Adaptive Android launcher icon (two-layer source). Linux `.desktop` and icon-cache install.
- Splash tied to real asset-load completion. Today it is a fade, hold, and fade timer.
- Per-window `Camera`. Meshes already draw into every window bound to that world. Those windows share the world's `ActiveCamera`. Each window's projection uses `window_size_for` (`Presentation.sizes`).
- Drag-region hole-punching so a `Button` inside a title-bar rect stays clickable.
- Visual verification of GL-window transparency on a real display. Linux and macOS overlay styles are untested.
- HTTP: Linux and macOS backends (today `Unsupported`), streaming bodies, download to a file, and progress.
- Open-file dialog results as an owned call, like `HttpCall`, instead of `FileDialogResultEvent` in the owner window's world.

Game concerns (do not implement in this repo): persist bus volumes in a settings file; that game's AI tests.

## Names

| Name | Role |
| --- | --- |
| Wind | Product name |
| `wind-N` | Task code on commits and `feat/wind-N-…` branches |
| `engine` | CMake library and C++ namespace. Static in exported builds, shared in the editor build |
| `wind_editor` | Editor host executable |
| `ENGINE_EDITOR` | CMake switch for the editor build: shared `engine`, game as a module, `wind_editor` |
| `IGame` / `Engine<GameT>` | Game lifecycle contract and standalone windowed host |
| `AssetId` / `try_get` / `get` | GUID lookup; optional vs fatal |
| `IMaterial` / `CommandBuffer` | Draw contract |
| `ViewModel` / `ICommand` / `IPaint` | UI to game; named paint hole, not `CmdCustomDraw` |
| `WindowId` / `WindowDesc` | One OS window. `kPrimaryWindow` is the first and belongs to the game |

## See also

- [Principles](Principles.md)
- [Boundaries](Boundaries.md)
- [Editor Plan](Editor%20Plan.md)
- [Windowing](../features/Windowing.md)
