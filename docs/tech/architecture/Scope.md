---
tags: [architecture]
---

# Scope

Wind is a 2D C++ game engine for production titles. Games are `IGame` implementations on top of a static library in this repo; they do not vendor SDL or copy engine sources.

## In scope

- Lifecycle: init, main loop, shutdown.
- Window, GL context, input actions, mouse → UI. Multiple **windows** for one running game ([[features/Windowing]]).
- One `ecs::World` (no scene-graph `Node`). UI is `UiCanvas` + document instance + ViewModel. Document from XML or the C++ builder.
- Homemade ECS with EnTT as API reference only (not a dependency).
- Render abstractions + OpenGL 3.3 backend: materials, instance tint, layer sort; NanoVG executes `CmdDrawUI`.
- AssetsDb + GUID catalog: sidecar TOML `.meta`, `asset_guid` + `asset_codegen`.
- Audio: buses, SFX pool, music A/B, looping SFX handles on SDL3_mixer.
- Double-buffered event queues, not a callback bus.
- Boost.DI wiring of engine services.
- GoogleTest suite for engine logic (`engine_tests`).

## Out of scope

- Gameplay, levels, AI, menus of a specific game.
- 3D renderer, lighting, skeletal animation.
- A full physics engine (Box2D / rapier). Current physics is AABB/circle + velocity integration (a collision probe, not a solver).
- Networking, scripting VM, editor, asset pipeline GUI.
- DirectX / Metal backends (interfaces exist so they can be added).
- GPU / window golden-image tests and full `Engine<GameT>` boot in CI ([[architecture/Boundaries]]).
- Browser-grade CSS and full WPF (`ControlTemplate`, `VisualStateManager`, `x:Class` code-behind, C++ reflection).
- Sharing one process between multiple games (multiple `IGame` instances). One game, many windows is in scope.

## Non-goals (current product)

- Hot reload.
- 3D spatial audio, Doppler, HRTF.
- JSON / ScriptableObject sound banks in C++. Volume/pitch live in audio `.meta`.
- A C++ widget graph (`new Label()`, `onClick` lambdas, `MeasureOverride`). Use `ui::Node` / XML into the same `Element` tree.
- Transform parenting, scene-graph matrices, or auto Y-sort (`sort_mode = Y`).
- Per-pixel (framebuffer-alpha) click-through. Click-through is bounding-box hit-test ([[features/Windowing]]).
- Haptics waveforms / pattern playback (duration + intensity only).

## Backlog

Open engine work, not game concerns:

- Packed asset bundles (still GUID-addressed).
- Separate cue `Sound` that references a clip GUID (today one file = one cue).
- `Transform` parent / world-matrix chain.
- CSS `@import`; WPF `IValueConverter`, `Mode=TwoWay`.
- Widget-as-ECS-entity (would replace the XML instance tree inside `UiCanvas`).
- Input: SDL gamepad poll/bind (`ControlKind::GamepadButton` / `GamepadAxis` exist; events are not wired), WASD composites, action maps.
- Adaptive Android launcher icon (two-layer source). Linux `.desktop` + icon-cache install.
- Splash tied to real asset-load completion instead of a fade/hold/fade timer.
- Per-window `Camera` / world rendering (today `Renderable`s draw only into `kPrimaryWindow`; secondary windows are UI-only).
- Drag-region hole-punching so a `Button` inside a titlebar rect stays clickable.
- Visual verification of GL-window transparency on a real display; Linux/macOS overlay styles untested.

Game concerns (do not implement in this repo): persist bus volumes in a settings file; that game’s AI tests.

## Names

| Name | Role |
| --- | --- |
| Wind | Product name |
| `wind-N` | Task code on commits and `feat/wind-N-…` branches |
| `engine` | CMake static library and C++ namespace |
| `IGame` / `Engine<GameT>` | Game lifecycle contract and windowed host |
| `AssetId` / `try_get` / `get` | GUID lookup; optional vs fatal |
| `IMaterial` / `CommandBuffer` | Draw contract |
| `ViewModel` / `ICommand` / `IPaint` | UI → game; named paint hole, not `CmdCustomDraw` |
| `WindowId` / `WindowDesc` | One OS window; `kPrimaryWindow` is the first |

## See also

- [[architecture/Principles]]
- [[architecture/Boundaries]]
- [[features/Windowing]]
