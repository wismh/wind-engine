# Principles

Normative rules. If a change fights these, the change is wrong. As-built detail lives in modules and features. If that prose drifts from code, update the page in the same change.

## Goals

1. **Low coupling.** `Engine::init` constructs services and passes them to the game as `EngineServices`. No service locator (`Engine::get_audio()`).
2. **Clear ownership.** Engine owns window, GL, audio, and import. The game owns the `assets/` tree, `.meta` files, and generated `asset_ids.h`.
3. **One way to draw.** Game and ECS never call OpenGL. They push `Command`s; the backend executes them.
4. **Named input.** Gameplay binds controls to an interned `ActionId`, not raw keys in systems.
5. **Assets only by GUID.** `AssetsDb::get<T>(AssetId)` (fatal if the cooked asset is missing) or `try_get`. Named atlas frames: `get_sprite(id, name)`. No filenames in game code.
6. **Import settings live in `.meta`.** A bare PNG or WAV is not a texture or sound until its sidecar says how to load it.
7. **Audio is a system, not a filename firehose.** `IAudioSystem` plays `Sound` objects from the audio importer, not `PlaySoundEvent{"hit.wav"}`.
8. **Reusable across many production games.** Window title and size come from `IGame`. Do not size features to one title.
9. **Test the engine, not the games built with it.** Shared logic has GoogleTest coverage here. Gameplay stays in the game repo.
10. **Simulation is fixed-step.** Frame time drives present and audio fades. Gameplay and physics tick at a constant `fixed_delta_time`.
11. **UI is document + style + view-model.** XML and the C++ `ui::Node` builder produce the same `Element` tree. Style is CSS. UI to game is `ViewModel` / `ICommand` only. No `onClick`.
12. **Draw with materials, then sort.** `Renderable` is mesh + material + layer, not ad-hoc shader and texture pointers with undefined order.
13. **The editor hosts, the game is a guest.** In the editor the game is a module loaded on Play and unloaded on Stop. Nothing built from game code (systems, components, view-models, commands, `std::function`) outlives the game's worlds. Engine tools such as the UI Inspector and UI Profiler live in the editor, not in the game.

## Do not regress

1. Game logic does not include glad, SDL render, mixer, NanoVG, spdlog, or tinyxml2. The host includes `engine.h`. Game targets do not add `engine/src` to their include path.
2. Draw only through `CommandBuffer`. The public `Command` variant has no custom GL callback. World draws use `IMaterial`. UI custom primitives go through `IDrawList` from `IPaint`.
3. Load only through `AssetsDb` by `AssetId`. Gameplay uses `get` (fatal). Optional content uses `try_get` and handles `AssetError`.
4. Play audio only through `IAudioSystem` with a `Sound` from the audio importer (or a test double).
5. Simulation and gameplay cross-talk uses event queues (`send` / `read` / `flush_events`), not observer `Subscribe`. UI to game is `ICommand` on a `ViewModel` only.
6. Shared engine behavior ships with a GoogleTest, not only a game that seems to work.
7. Do not change an asset GUID after it is referenced. Move files with their `.meta`. Builtin GUIDs in `builtin_ids.h` are frozen.
8. `asset_codegen` never writes `.meta`. A missing sidecar is a failed build, not a random GUID in CI.
9. ECS is homemade and EnTT-shaped. Do not add EnTT as a submodule. Do not keep a Node graph beside `World`. No `Transform` parent.
10. Simulation uses `fixed_delta_time` on `Schedule::Fixed`. One-shot clicks run on `Schedule::Frame`, `Phase::Game`.
11. UI documents are XML assets and/or `ui::Node` into the same `Element` tree. Games do not build a parallel widget graph.
12. All engine APIs are main-thread only.
13. `MouseConsumed` is cleared once at the start of each process frame (`ui::reset_pointer_frame`), not inside `ui::begin_frame` and not at the end of the frame. `begin_frame` still fits canvases and syncs the inspector and profiler.
14. No engine code assumes a single global window. A `UiCanvas` is keyed by `WindowId`. A window belongs to one world. That world's meshes go to each of its windows. Window size, pointer, and mouse consumption live on the process `Presentation`, not in a world's `ctx`.
15. Window, GL, and SDL platform calls stay behind `WindowManager` in `src/render/opengl/`. `#if defined(_WIN32)` branches do not leak a Win32 type into `include/`.

## Constraints

- An exported game links `engine` statically. `engine` is a shared library only in the editor build (`ENGINE_EDITOR`), and a game module must be built against the same engine build as the editor that loads it.
- CMake 3.20 or newer (`cmake_minimum_required` in `CMakeLists.txt`). C++23 (MSVC, clang, or gcc).
- SDL3 and SDL3_mixer. This mixer build enables WAV only. FLAC, Vorbis, MP3, MIDI, Opus, and the other `SDLMIXER_*` formats are OFF.
- Desktop GL is OpenGL 3.3 Core via glad. Shaders are GLSL 330 wrapped in XML `.shader`. Web is WebGL2. Android is GLES 3.0. `shader_adapt` rewrites GLSL 330 to GLSL 300 ES when the GLES profile is on.
- `.meta` files are TOML (tomlplusplus).
- The assets root comes from the SDL executable base path plus `assets`, not the process working directory. Web uses `/assets`. Android stages that tree onto internal storage. See [Runtime Assets](../build/Runtime%20Assets.md).
- Asset GUIDs are exactly 32 lowercase hex characters, stable once referenced. Never reuse a GUID.
- Logging: spdlog only in `src/`. Public facade `engine::log::{info,warn,error}`.
- Fatal errors: `IFatalError` hook. Normal gameplay does not use C++ exceptions for missing assets.
- Public headers may include glm. They must not include SDL, glad, NanoVG, spdlog, or mixer.

## See also

- [Scope](Scope.md)
- [Boundaries](Boundaries.md)
- [Overview](Overview.md)
