# Scope

Wind is a 2D C++ game engine for production titles. Games are `IGame` implementations on top of a static library in this repo. They do not vendor SDL or copy engine sources.

## In scope

- Lifecycle: init, main loop, shutdown.
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
- GoogleTest suite for engine logic (`engine_tests`).

## Out of scope

- Gameplay, levels, AI, and menus of a specific game.
- 3D renderer, lighting, skeletal animation.
- A full physics engine (Box2D, rapier). Current physics is AABB and circle overlap plus velocity integration. It is a collision probe, not a solver.
- Networking, a scripting VM, an editor, an asset-pipeline GUI. A server binary is not part of this change.
- DirectX and Metal backends. The interfaces exist so they can be added.
- GPU and window golden-image tests, and a full `Engine<GameT>` boot in CI. See [Boundaries](Boundaries.md).
- Browser-grade CSS and full WPF (`ControlTemplate`, `VisualStateManager`, `x:Class` code-behind, C++ reflection).
- Sharing one process between multiple games (multiple `IGame` instances). One game, many windows is in scope.

## Non-goals (current product)

- Hot reload.
- 3D spatial audio, Doppler, HRTF.
- JSON or ScriptableObject sound banks in C++. Volume and pitch live in audio `.meta`.
- A C++ widget graph (`new Label()`, `onClick` lambdas, `MeasureOverride`). Use `ui::Node` or XML into the same `Element` tree.
- Transform parenting, scene-graph matrices, or auto Y-sort (`sort_mode = Y`).
- Per-pixel (framebuffer-alpha) click-through. Click-through is a bounding-box hit-test. See [Windowing](../features/Windowing.md).
- Haptics waveforms and pattern playback. Duration and intensity only.
- Bidirectional text, complex-script shaping, and OS `setlocale`. String tables cover left-to-right languages whose glyphs are in the UI font.

## Backlog

Open engine work, not game concerns:

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

Game concerns (do not implement in this repo): persist bus volumes in a settings file; that game's AI tests.

## Names

| Name | Role |
| --- | --- |
| Wind | Product name |
| `wind-N` | Task code on commits and `feat/wind-N-…` branches |
| `engine` | CMake static library and C++ namespace |
| `IGame` / `Engine<GameT>` | Game lifecycle contract and windowed host |
| `AssetId` / `try_get` / `get` | GUID lookup; optional vs fatal |
| `IMaterial` / `CommandBuffer` | Draw contract |
| `ViewModel` / `ICommand` / `IPaint` | UI to game; named paint hole, not `CmdCustomDraw` |
| `WindowId` / `WindowDesc` | One OS window. `kPrimaryWindow` is the first |

## See also

- [Principles](Principles.md)
- [Boundaries](Boundaries.md)
- [Windowing](../features/Windowing.md)
