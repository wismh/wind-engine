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

1. Game logic does not include glad, SDL render, mixer, spdlog, or tinyxml2. The host includes `engine.h`. Game targets do not add `engine/src` to their include path.
2. Draw only through `CommandBuffer`. The public `Command` variant has no custom GL callback. World draws use `IMaterial`. UI custom primitives go through `IDrawList` from `IPaint`.
3. Load only through `AssetsDb` by `AssetId`. Gameplay uses `get` (fatal). Optional content uses `try_get` and handles `AssetError`.
4. Play audio only through `IAudioSystem` with a `Sound` from the audio importer (or a test double).
5. Simulation and gameplay cross-talk uses event queues (`send` / `read` / `flush_events`), not observer `Subscribe`. UI to game is `ICommand` on a `ViewModel` only.
6. Shared engine behavior ships with a GoogleTest, not only a game that seems to work.
7. Do not change an asset GUID after it is referenced. Move files with their `.meta`. Builtin GUIDs in `builtin_ids.h` are frozen.
8. `asset_codegen` never writes `.meta`. A missing sidecar is a failed build, not a random GUID in CI.
9. ECS is homemade and EnTT-shaped. Do not add EnTT as a submodule. Do not keep a Node graph beside `World`. `Transform` has no parent field. The hierarchy is a separate `Parent` component plus a derived `WorldTransform` (see [ECS](../modules/ECS.md)); `Parent` and `Transform` are the replicated state, `WorldTransform` is derived and never replicated.
10. Simulation uses `fixed_delta_time` on `Schedule::Fixed`. One-shot clicks run on `Schedule::Frame`, `Phase::Game`.
11. UI documents are XML assets and/or `ui::Node` into the same `Element` tree. Games do not build a parallel widget graph.
12. All engine APIs are main-thread only. `JobSystem::parallel_for` and `JobSystem::submit` are the calls that run a function off that thread, and that function must not call engine APIs, except the pure functions documented as callable from any thread: `compress` and `decompress` (`<engine/core/compression.h>`). A submitted job returns data; its `JobCall` hands it to the main thread.
13. `MouseConsumed` is cleared once at the start of each process frame (`ui::reset_pointer_frame`), not inside `ui::begin_frame` and not at the end of the frame. `begin_frame` fits canvases and commits the profiler frame. It opens no tool window: the inspector and profiler panels are editor canvases in the editor's world.
14. No engine code assumes a single global window. A `UiCanvas` is keyed by `WindowId`. A window belongs to one world. That world's meshes go to each of its windows. Window size, pointer, and mouse consumption live on the process `Presentation`, not in a world's `ctx`.
15. Window, GL, and SDL platform calls stay behind `WindowManager` in `src/render/opengl/`. `#if defined(_WIN32)` branches do not leak a Win32 type into `include/`.
16. Game code tests `ENGINE_EDITOR` only for tools (an editor-only debug overlay, a cheat panel, extra logging). Gameplay, simulation, saves, and UI flow must not depend on it, or the game in the editor behaves unlike the exported game. The same goes for `ENGINE_UI_PROFILER` and `ENGINE_CLI_SERVER`, which the editor has in every configuration.

## Deterministic simulation

Engine simulation gives the same bits for the same inputs on every supported target, compiler and configuration. It is the default, with no switch, no second mode and no second SDK: a networked game (lockstep inputs, a fluid every peer steps) relies on it, and every game pays for it, so its cost stays near zero (measured per platform in a backlog ticket). Simulation is everything the engine runs on `Schedule::Fixed`: 2D and 3D physics, SDF fields and colliders, `Fluid2D`, grid and ray queries, `update_world_transforms` and the transform helpers they call (`src/ecs`, `include/engine/ecs`). Rendering, UI, audio and visual particles are not simulation and may use anything.

1. **No contraction and no fast-math.** Every translation unit that compiles engine or glm code takes the same float flags: `/fp:precise` on MSVC (which does not contract since Visual Studio 2022; `/fp:contract` is never passed), `/fp:precise /clang:-ffp-contract=off` on clang-cl, `-ffp-contract=off -fno-fast-math` on clang, gcc and Emscripten. They are engine-wide, not per module: the root sets them on every target of its tree and on `engine`'s interface, and an SDK's `WindConfig.cmake` on the imported `engine`, so a game compiles inline engine and glm code as the engine did (otherwise two definitions of one inline function differ, and the linker keeps either). The configure refuses `/fp:fast`, `/fp:contract`, `-ffast-math`, `-Ofast`, `-ffp-contract=on|fast`, `-funsafe-math-optimizations` and `-mfma` ([CMake](../build/CMake.md#deterministic-floating-point)).
2. **No libm transcendentals.** The C library's `sin`, `cos`, `atan2`, `exp`, `pow`, `log` (and glm's `angleAxis`, `rotate`, `eulerAngles`, `slerp` on top of them) differ in the last bits between MSVC, glibc, libc++ and Emscripten. Simulation uses `det_sin_cos`, `det_sin`, `det_cos` and `det_atan2` (`<engine/core/det_math.h>`, `+ - * /` in double, correctly rounded to float for almost every input) and `angle_axis`, `rotation_z`, `euler_xyz` (`<engine/ecs/transform.h>`, built on them). Where a value can come from a matrix (a heading from the normalized X axis), it does. `sqrt`, `floor`, `fmod` and the other correctly rounded or exact operations are allowed. `engine_tests` (`SimulationMathCheck`) scans the simulation sources and fails on a new call.
3. **No approximate SIMD and no FMA intrinsics.** No `rsqrtps`, `rcpps` or their NEON forms, no FMA intrinsics, no `GLM_FORCE_*` SIMD defines (glm stays scalar). Checked by the same scan.
4. **Fixed evaluation and reduction order.** Sums run in index order, never in worker order; a `parallel_for` pass is a gather that writes only its own slots (the 2D solver's color sets, `Fluid2D`). The result does not depend on the worker count.
5. **One float mode.** Simulation runs with IEEE denormals (no flush-to-zero, no denormals-are-zero) and round to nearest, the only mode WebAssembly has. `World::run(Schedule::Fixed)` sets it on the calling thread, and `JobSystem` sets it on each worker when it starts and before every `parallel_for` batch, because a game, a driver or an audio library may leave a thread in another mode (`src/core/float_mode.cpp`: MXCSR on x64, FPCR on arm64). A thread already in it pays one register read.
6. **64-bit targets and WebAssembly only.** No 32-bit x86 (x87) and no 32-bit ARM (ARMv7 NEON flushes denormals): the configure fails for a 4-byte pointer outside Emscripten, and `float_mode.cpp` refuses to compile for x86.

State hashes for desync detection: `Fluid2D::state_hash`, `SdfField2D::hash`, `physics_state_hash_2d` and `physics_state_hash_3d` (FNV-1a 64 over the bits). Golden tests (`tests/simulation_golden_test.cpp`) step fixed scenes (a 2D stack with a compound ramp, SDF terrain carved twice, a dam break with bodies falling into the water, a 3D stack) and compare those hashes with checked-in values, with and without workers. A changed hash fails; it is updated on purpose, with the reason in the commit. Run on (LQ-63, same hashes everywhere): Windows MSVC x64 Debug and Release, Windows clang-cl 22 x64 Release, WebAssembly (Emscripten, Debug, under Node). Not run yet: Linux clang or gcc x64 and an arm64 target (Android, macOS); the arm64 float-mode code was only compiled (clang for aarch64, clang-cl for arm64 Windows). Add a target here when its run matches.

## Constraints

- An exported game links `engine` statically. `engine` is a shared library only in the editor build (`ENGINE_EDITOR`), and a game module must be built against the same engine build as the editor that loads it, with the same C runtime: `DebugGame` or `Release` against a Release SDK, `Debug` against a Debug SDK.
- CMake 3.20 or newer (`cmake_minimum_required` in `CMakeLists.txt`). C++23 (MSVC, clang, or gcc).
- SDL3 and SDL3_mixer. This mixer build enables WAV only. FLAC, Vorbis, MP3, MIDI, Opus, and the other `SDLMIXER_*` formats are OFF.
- Desktop GL is OpenGL 3.3 Core via glad. Shaders are GLSL 330 wrapped in XML `.shader`. Web is WebGL2. Android is GLES 3.0. `shader_adapt` rewrites GLSL 330 to GLSL 300 ES when the GLES profile is on.
- `.meta` files are TOML (tomlplusplus).
- The assets root comes from the SDL executable base path plus `assets`, not the process working directory. Web uses `/assets`. Android stages that tree onto internal storage. See [Runtime Assets](../build/Runtime%20Assets.md).
- Asset GUIDs are exactly 32 lowercase hex characters, stable once referenced. Never reuse a GUID.
- Logging: spdlog only in `src/`. Public facade `engine::log::{info,warn,error}`.
- Fatal errors: `IFatalError` is the single fatal exit and `report` never returns. `try_*` returns `std::expected`; `get` is fatal. Normal gameplay does not use C++ exceptions for missing assets.
- Public headers may include glm. They must not include SDL, glad, spdlog, or mixer.

## See also

- [Scope](Scope.md)
- [Boundaries](Boundaries.md)
- [Overview](Overview.md)
