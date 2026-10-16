# Core

Host, time, input polling, logging, fatal errors, platform paths, and, when `ENGINE_WITH_WINDOW` is on, `EngineHost`, `Engine<GameT>`, the game entry macro, and the SDL runtime.

Frame order is [Runtime Loop](../architecture/Runtime%20Loop.md). Window behavior is [Windowing](../features/Windowing.md). Named controls are [Input Mapper](../features/Input%20Mapper.md).

## What the game implements

`IGame` (`include/engine/igame.h`):

| Hook | `GameBase` |
| --- | --- |
| `primary_window()` | `WindowDesc{}` — title `"Game"`, size 800×600, resizable |
| `window_icon()` | `nullopt`. The OS icon stays. A set id is loaded in `EngineHost::attach_game` after the catalogs |
| `world()` | the world `GameBase` created with `Worlds::add` |
| `on_start` / `on_quit` | empty |

`GameBase` takes `Worlds&`. The host runs `Schedule::Fixed` and `Schedule::Frame`. A game adds its systems in `on_start`.

`EngineGame` requires `GameT` to derive from `IGame` and to be constructible from `const EngineServices&`.

`EngineServices` holds references only. `EngineHost` owns the objects.

## Entry point

A game's `main.cpp` is one line after its includes:

```cpp
#include <engine/game_entry.h>

#include <game/game.h>

ENGINE_GAME(game::Game)
```

| Build | `ENGINE_GAME(GameClass)` expands to |
| --- | --- |
| Exported (no `ENGINE_GAME_MODULE`) | `int main()` that runs `Engine<GameClass>::init` and `run`. Android aliases `SDL_main` to it at link time |
| Editor module (`ENGINE_GAME_MODULE`, set by `engine_add_game` in SDK mode or under `ENGINE_EDITOR`) | on MSVC an `#error` when `_DEBUG` or `_ITERATOR_DEBUG_LEVEL` differs from the engine's (`ENGINE_BUILD_DEBUG_CRT`, `ENGINE_BUILD_ITERATOR_DEBUG_LEVEL` in `<engine/build_id.h>`, see [CMake](../build/CMake.md#crt-guard)), a `static_assert` on `EngineGame`, then four `extern "C"` exports |

The module exports (`include/engine/core/game_module.h`):

| Symbol | Type | Body |
| --- | --- | --- |
| `wind_create_game` | `CreateGameFn`: `IGame* (*)(const EngineServices&)` | `new GameClass(services)` plus `assets::register_game_components` on `game->world().ctx<ecs::WorldSerializer>()` when `<component_io.h>` is present |
| `wind_destroy_game` | `DestroyGameFn`: `void (*)(IGame*)` | `delete game` |
| `wind_game_build_id` | `GameBuildIdFn`: `const char* (*)()` | `kBuildIdCStr`, baked when the game compiles |
| `wind_register_game_components` | `RegisterGameComponentsFn`: `void (*)(ecs::WorldSerializer&)` | `assets::register_game_components(serializer)` when `<component_io.h>` is present (no-op otherwise) |

`kCreateGameSymbol`, `kDestroyGameSymbol`, `kGameBuildIdSymbol`, and `kRegisterGameComponentsSymbol` hold the names for a loader. `ENGINE_GAME_EXPORT` is `extern "C" __declspec(dllexport)` on Windows and `extern "C"` with default visibility elsewhere. `wind_game_build_id` does not call `engine::build_id()`: inside the editor that reaches the editor's `engine.dll` and always matches.

## Game module loader

Same header, window builds only (`src/core/game_module.cpp`, SDL `SDL_LoadObject`). The editor uses it on Play.

`load_game_module(module, live_root)` returns `std::expected<GameModule, GameModuleError>`:

1. `module` must be a regular file, else `ModuleError::Missing`.
2. It creates the first free `<live_root>/<n>/` (n = 1, 2, ...) and copies the module there, plus the `.pdb` beside it when there is one. A failed module copy is `CopyFailed`. A failed `.pdb` copy only warns.
3. It loads the copy. Failure is `LoadFailed` with `SDL_GetError()`.
4. It resolves the required exports (`wind_create_game`, `wind_destroy_game`, `wind_game_build_id`) and optional `wind_register_game_components`. A missing required export is `MissingSymbol` with its name.
5. It compares `wind_game_build_id()` with `engine::build_id()`. A difference is `BuildIdMismatch` with both ids.

Any failure unloads the copy and deletes its directory. `GameModuleError` is the `ModuleError` plus a detail string. `to_string(ModuleError)` and `describe(error)` give one line for a status bar.

`GameModule` is move-only. `create(services)` and `destroy(game)` call the lifecycle exports; `register_components(serializer)` invokes `wind_register_game_components` when present (`has_component_registration()`). `build_id()`, `source_path()`, and `live_path()` describe the load. The destructor unloads the copy and deletes its directory. Nothing the game built (including `WorldSerializer` entries registered from the module) may outlive it: destroy the game's worlds, serializers, and the game first.

`purge_game_module_copies(live_root)` deletes every directory under `live_root` and returns how many went away. A copy another process still has loaded stays.

## `EngineHost`

`include/engine/core/engine_host.h`, `src/core/engine_host.cpp`. Window builds only. It owns `EngineRuntime`, `SdlFatalError`, `AssetsDb`, `InputSystem`, `AudioSystem`, `HapticsSystem`, `HttpClient`, `ProcessLauncher` ([Process](Process.md)), and `Worlds`. `Engine<GameT>` sits on it. The editor ([Editor](../features/Editor.md)) sits on it too.

| Call | Does |
| --- | --- |
| `init()` | SDL video, `log::init`, every service, the input router, the fatal hook. Returns `std::expected<void, EngineError>`: `VideoInit` when SDL video does not start, success at once after a success |
| `services()` | `EngineServices` over the owned objects. Valid after `init` |
| `fatal()` | the `IFatalError` the services use |
| `open_primary(desc)` | creates `kPrimaryWindow`, starts audio and haptics, starts HTTP (a failure only warns, see [Net](Net.md)), sets the graphic factory and assets root, loads the engine catalog and `builtin::font_ui`, then `Worlds::set_deps`. A failure disposes the host and returns `WindowCreate`, `AudioInit`, `HapticsInit`, `AssetsRoot`, `EngineCatalog`, or `UiFont`. Returns `VideoInit` when `init` has not succeeded |
| `assets_root()` | the runtime assets root ([Runtime Assets](../build/Runtime%20Assets.md)) |
| `load_catalog(dir)` | `<dir>/catalog.toml` with `dir` as its files root. A missing file (`MetaError::Io`) is success. Other errors return the `MetaError`. The game catalog and the editor's own catalog both load this way |
| `unload_catalog(dir)` | `AssetsDb::unload_catalog(dir)`. Pass the same path `load_catalog` got |
| `attach_game(game)` | window icon, `bind_window(kPrimaryWindow)`, `enable_ui`, `enable_audio`, publish the window size with a resize event, `ui::apply_canvas_fit` |
| `detach_game()` | the window half of undoing `attach_game`: `unbind_window(kPrimaryWindow)`, clear its command buffer, reset its UI painter and register `builtin::font_ui` again, clear its drag region and click-through, overlay mode back to `Auto`, `ApplicationState::paused` false. The editor calls it on Stop |
| `run(hooks)` | `EngineRuntime::run` with `RunHooks` and the `HttpClient` and `ProcessLauncher` the loop polls, then `dispose`. Returns 1 before a successful `open_primary` |
| `dispose()` | disposes audio, haptics, HTTP, and the process launcher (ending the programs its calls still own) and shuts the runtime down. Also run by the destructor. A second call is a no-op |

`set_deps` registers simulation systems on worlds that already exist and on every later `Worlds::add`. `enable_ui` and `enable_audio` add the rest. Each registration is guarded by its `ctx` flag, so a world never gets a system twice, whether it was added before or after `set_deps`.

`Engine<GameT>` (`include/engine/core/engine.h`) is a thin template: `init` runs `EngineHost::init`, constructs `GameT` from `services()`, `open_primary(game.primary_window())`, `load_catalog(assets_root())` (an error disposes the host and returns `GameCatalog`), and `attach_game`. It returns `std::expected<void, EngineError>`. `run` passes `RunHooks` that call `on_start` and `on_quit`. The game is destroyed before the host.

### Startup errors

`EngineError` (`include/engine/core/engine_error.h`, header only, all builds) names why the engine could not start: `VideoInit`, `WindowCreate`, `AudioInit`, `HapticsInit`, `AssetsRoot`, `EngineCatalog`, `UiFont`, `GameCatalog`. `to_string(error)` is the enumerator name (logs, tests), `message(error)` the sentence for the dialog. A startup failure is returned, never reported where it happens: `EngineHost::init`, `open_primary`, and `Engine<GameT>::init` show nothing and exit nothing, and each failure after `init` disposes the host first. The entry point is the one place that tells the user. `report_startup_error(error)` (declared in `engine_host.h`) logs the name and shows `message(error)` through a fresh `SdlFatalError`, so it also works for `VideoInit`, when the host has no fatal hook yet. `ENGINE_GAME`, the bench `main`, and `EditorApp::start` call it; a failure that belongs to one entry (the editor's missing project or its own catalog) calls its `IFatalError` directly. The detail of a `MetaError` from the game catalog is not part of the error. `EngineHost` and `EngineRuntime` have a constructor taking an `IPresentation` so a test can stand in for SDL.

`SplashScreen` lives on this header (enabled, `builtin::splash_wind`, fade 0.4s, hold 1.0s, fade 0.4s). `IGame` has no splash method. A game calls `ui::show_splash`. See [UI](UI.md).

## Time

| Constant | Value |
| --- | --- |
| `kFixed` | `1/60` |
| `kMaxFixedSteps` | 8 |
| `kMaxFrameDt` | 0.25 |

Each world has a `FixedStepClock` on its own `Time`. `advance` clamps wall dt, accumulates only while the process is not paused and that world's stepping flag is true, and zeros the accumulator when the step cap is hit. `delta_time` is the clamped frame dt even while paused or not stepping. `alpha` is `accumulator / kFixed`.

`ApplicationState` is one per process, on `Worlds`. It starts `running = true`, `paused = false`. `quit()` sets `running` false. `GameLoop::begin` sets `running` true again after `on_start`. `set_stepping(world, false)` skips that world's fixed steps and frame and does not accumulate. An OS pause skips Fixed on every world. A window-bound world still runs Frame while the process is paused, so its UI stays up. A windowless world runs neither while paused.

Lifecycle (`include/engine/core/app_lifecycle.h`):

| Event | Effect |
| --- | --- |
| `WillEnterBackground` | `paused = true` |
| `DidEnterForeground` | `paused = false` |
| `Terminating` | `quit()` |

The Android back key is not a lifecycle event. The engine never quits on it; it is `KeyCode::AcBack`, and the game binds it. See [Input Mapper](../features/Input%20Mapper.md#android-back).

## Platform and user data

`current_platform()` is Web when `__EMSCRIPTEN__`, Android when `__ANDROID__`, otherwise Native.

| Platform | Loop | Graphics profile |
| --- | --- | --- |
| Native | blocking `while` | OpenGL 3.3 Core |
| Web | `requestAnimationFrame` | WebGL2, ES |
| Android | blocking | GLES 3.0 |

`audio_requires_user_gesture()` is true only on Web. `haptics_has_amplitude_control(Platform)` is true only for Android. That predicate does not encode the API 26 runtime check. See [Haptics](Haptics.md).

`executable_directory()` (`include/engine/core/platform.h`) is the directory of the running executable: `GetModuleFileNameW` (the buffer grows until the path fits) on Windows, `/proc/self/exe` on Linux, `_NSGetExecutablePath` on macOS, an empty path elsewhere or when the system cannot say. It needs no SDL and no `Engine::init`. The editor finds its SDK as the parent of this directory in a batch run, where no `EngineHost` gives it an assets root ([Editor](../features/Editor.md#export)).

`user_data_directory(organization, application)` returns a writable directory. The game owns the file format.

| Result | When |
| --- | --- |
| Desktop SDL pref path | `%APPDATA%`, `$XDG_DATA_HOME` or `~/.local/share`, `~/Library/Application Support` |
| Web | `/storage/<org>/<app>/`. `engine_add_sdl3` forces `SDL_EMSCRIPTEN_PERSISTENT_PATH` to `/storage`. The browser may flush a write a few frames later |
| Android | `<internal storage>/user/`. SDL's Android pref path is the storage root and ignores the two name strings. `applicationId` isolates apps |
| `errc::invalid_argument` | empty, spaced, trailing-dot, or Windows-device path segment |
| `errc::function_not_supported` | `ENGINE_WITH_WINDOW` is off |
| `errc::io_error` | SDL could not create the pref directory |

Main thread only. On Android, call it after `Engine::init` (`EngineHost::init`).

Assets roots: [Runtime Assets](../build/Runtime%20Assets.md).

## Random and Config

`engine::Random` (`include/engine/core/random.h`, `src/core/random.cpp`) is PCG32 (XSH-RR). `Random(seed, stream)` and `seed(seed, stream)` follow the reference initialisation, so seed 42 on stream 54 gives `0xa15c02b7`, `0x7b47f409`, ... A given seed and stream give the same sequence on every platform: `next_u32`, `next_u64` (two draws, high word first), `next_float` (`(next_u32() >> 8) / 2^24`, in [0, 1)), `uniform(int, int)` (inclusive, rejection sampling, no modulo bias), `uniform(float, float)` (`lo + (hi - lo) * next_float()`), `chance(p)`, and `pick(range)` are written in the engine, not taken from `<random>`, whose distributions differ between standard libraries. A default-constructed `Random` seeds from the clock. A world holds one as `world.ctx<Random>()`; the first access builds it, so it is clock-seeded unless the game seeds it. `IGame::random_seed()` returns an optional fixed seed; `EngineHost::attach_game` applies it to `game.world()`'s `Random` (tests and replays, or a daily seed). A world added later, or another world, calls `ctx<Random>().seed(n)` itself.

`engine::Config` (`include/engine/core/config.h`, `src/core/config.cpp`) is a flat table of strings, ints, floats, and bools stored as a TOML file. A key is `name` or `section.name`: one level, written as `name` in the file or in `[section]`. `Config::load(path)` treats a missing file as an empty config and returns `ConfigError::InvalidToml` for text that does not parse, and `ConfigError::Unsupported` for valid TOML that is not a flat table of scalars (an array, a date, a table in a table). The old file is never touched by a failed load. `save(path)` creates the parent directories, writes `<name>.tmp` beside the file, and renames it over the file; a failed write or rename removes the temp file and keeps the old file, and returns `ConfigError::Io`. A getter takes a default and returns it for a missing key or another type (`get_float` also reads an int). A setter with an invalid key (`Config::valid_key`) or a key that is both a value and a section is an `assert` and writes nothing. `user_config_path(organization, application, file_name)` is `user_data_directory(...) / file_name` and fails like it. tomlplusplus stays in `src/core/config.cpp`; the header has no toml type.

## Log and fatal errors

`engine::log::init()` is a null sink for tests. `init(exe_dir)` writes `<exe_dir>/game.log`. `info`, `warn`, and `error` are the public facade. spdlog stays in `src/core/log.cpp`.

`IFatalError` (`include/engine/resources/fatal_error.h`) is the single fatal exit. `report` is `[[noreturn]]`: no caller writes code after it, and an implementation ends the process (or, in a test, throws). Two public contracts lead to it. `try_*` (I/O, mods, parsing) returns `std::expected` and never reports. `get` (a programmer or shipping-data bug) reports. `assert` stays for engine invariants and `log::warn` for content quality.

| Implementation | `report` |
| --- | --- |
| `SdlFatalError` (`ENGINE_WITH_WINDOW` only) | SDL message box, `ApplicationState::quit()` when attached, then `std::exit(1)` |
| `DefaultFatalError` (`include/engine/resources/default_fatal_error.h`, `default_fatal_error()`) | writes the message to stderr, then `std::abort()`. Used where no host supplied one |
| a test double | throws, so a test asserts on the report (`tests/fixtures/fatal_fakes.h`: `FatalReport`) |

`EngineSystemDeps::fatal` left unset becomes `default_fatal_error()` when `register_*_systems` runs, so the systems always dereference it. `Worlds::set_deps` fills in the `Worlds` fatal instead. `EngineSystemDeps` null members mean "subsystem not connected" and are read once, at registration, not per frame:

| Dep | Without it |
| --- | --- |
| `commands` and `commands_for_window` both unset | `run_render` and `run_ui_render` are not registered |
| `assets` | the document-clone and stylesheet-merge systems are not registered. `run_bind` still binds whatever `UiInstance` a canvas already has |
| `assets` or `audio` | `run_audio` is not registered, so `PlaySfxEvent` and `PlayMusicEvent` are never read and nothing reports (the headless contract) |

`AudioSystemsRegistered` is set by `register_audio_systems` even when no audio system was added; a second call with other deps does not add one. `run_stylesheets` warns once per world and `AssetId` about a stylesheet missing from the catalog, and looks it up again each frame because a catalog loaded later can supply it.

`IBindErrorSink` (`ui/document.h`) is not a fatal exit: `apply_bindings` keeps walking after a failure, so it reports to a sink that returns.

## Always compiled

| File | Role |
| --- | --- |
| `src/core/build_info.cpp` | `build_id()` returns `kBuildId` as compiled into `engine` |
| `src/core/fixed_step.cpp` | accumulator |
| `src/core/host.cpp` | fake-canvas host |
| `src/core/input_system.cpp` | bind table and event enqueue |
| `src/core/log.cpp` | spdlog |
| `src/core/platform.cpp` | paths, staging, `executable_directory`, `user_data_directory` |
| `src/core/random.cpp` | `Random`, PCG32 |
| `src/core/config.cpp` | `Config` over tomlplusplus, `user_config_path` |
| `src/core/frame_step.cpp` | `flush_worlds`, `simulate_worlds` |
| `src/core/worlds.cpp` | process worlds, window binding, per-world clocks |
| `src/core/game_loop.cpp` | frame clock and `RunHooks`. Calls `IPresentation`, not SDL |
| `src/core/web_loop.cpp` | `MainLoopPolicy`, `LoopShutdown` |
| `src/core/file_dialog_call.cpp` | `FileDialogCall`, an owned open-file dialog; answers wait in `CallCompletions` (`src/core/call_completions.h`) |
| `src/core/app_lifecycle.cpp` | pause, resume, terminate |
| `src/core/back_key_filter.cpp` | `BackKeyFilter`: whether an `AcBack` key event reaches `InputSystem` or only dismisses text input |
| `src/cli/cli_server.cpp` | loopback server. The translation unit is empty without `ENGINE_CLI_SERVER` |

## Only with `ENGINE_WITH_WINDOW`

| File | Role |
| --- | --- |
| `include/engine/core/engine.h` | `Engine<GameT>::init`, `run`, `dispose` over `EngineHost` |
| `include/engine/game_entry.h` | `ENGINE_GAME(GameClass)` |
| `src/core/engine_host.cpp` | `EngineHost`: services, primary window, catalogs, game attach and detach, run |
| `src/core/engine_runtime.cpp` | owns `IPresentation` and `GameLoop` |
| `src/core/engine_instantiate.cpp` | explicit instantiation of `Engine<WindowSmokeGame>` |
| `src/core/sdl_fatal_error.cpp` | message box, quit, exit |
| `src/resources/default_fatal_error.cpp` | stderr and abort |
| `src/core/game_module.cpp` | `load_game_module`, `GameModule`, `purge_game_module_copies` |
| `src/render/opengl/sdl_gl_presentation.cpp` | SDL init, poll, windows, GL factory |

`IPresentation` (`src/core/presentation.h`) is the private seam `GameLoop` calls: poll, sync, draw, fonts, images, the per-window UI cache reset, window control.

## Public headers

`include/engine/engine.h` is the umbrella. It also includes `core/engine.h` when `ENGINE_WITH_WINDOW` is set.

- `include/engine/igame.h`
- `include/engine/log.h`
- `include/engine/core/application_state.h`
- `include/engine/core/app_lifecycle.h`
- `include/engine/core/build_info.h`
- `include/engine/core/config.h`
- `include/engine/core/engine.h`
- `include/engine/core/engine_error.h`
- `include/engine/core/engine_host.h`
- `include/engine/core/engine_runtime.h`
- `include/engine/core/engine_services.h`
- `include/engine/core/export.h`
- `include/engine/core/fixed_step.h`
- `include/engine/core/game_module.h`
- `include/engine/core/host.h`
- `include/engine/core/input_system.h`
- `include/engine/core/key_code.h`
- `include/engine/core/platform.h`
- `include/engine/core/random.h`
- `include/engine/core/run_hooks.h`
- `include/engine/core/sdl_fatal_error.h`
- `include/engine/resources/default_fatal_error.h`
- `include/engine/core/time.h`
- `include/engine/core/web_loop.h`
- `include/engine/core/file_dialog.h`
- `include/engine/core/window_desc.h`
- `include/engine/core/window_control.h`
- `include/engine/core/worlds.h`
- `include/engine/game_entry.h` (not in the umbrella; a game's `main.cpp` includes it)

## Tests

`tests/cmake_sanity_test.cpp`, `tests/host_test.cpp`, `tests/time_test.cpp`, `tests/input_test.cpp`, `tests/log_test.cpp`, `tests/platform_test.cpp`, `tests/web_loop_test.cpp`, `tests/android_lifecycle_test.cpp`, `tests/android_assets_test.cpp`, `tests/window_icon_test.cpp`, `tests/window_style_test.cpp`, `tests/event_window_test.cpp`, `tests/cli_server_test.cpp`, `tests/worlds_test.cpp`, `tests/game_loop_test.cpp` (`RunHooks` order, and `RunHooks::cli` answered without a primary world, with a fake `IPresentation`), `tests/engine_error_test.cpp` (names and messages), `tests/engine_host_test.cpp` (a failing `init_video` returns `VideoInit` over a fake `IPresentation`, no fatal report; window builds only), `tests/game_entry_test.cpp` (module exports, window builds only), `tests/game_module_test.cpp` (loader against the fixture modules, editor build only; skipped elsewhere), `tests/file_dialog_test.cpp` (`FileDialogCall`), `tests/random_test.cpp` (golden sequences, ranges, streams), `tests/config_test.cpp` (round trip, defaults, malformed input, failed save).

## See also

- [Runtime Loop](../architecture/Runtime%20Loop.md)
- [Module Map](../architecture/Module%20Map.md)
- [CMake](../build/CMake.md)
