# Core

Host, time, input polling, logging, fatal errors, platform paths, and, when `ENGINE_WITH_WINDOW` is on, `Engine<GameT>` plus the SDL runtime.

Frame order is [Runtime Loop](../architecture/Runtime%20Loop.md). Window behavior is [Windowing](../features/Windowing.md). Named controls are [Input Mapper](../features/Input%20Mapper.md).

## What the game implements

`IGame` (`include/engine/igame.h`):

| Hook | `GameBase` |
| --- | --- |
| `primary_window()` | `WindowDesc{}` — title `"Game"`, size 800×600, resizable |
| `window_icon()` | `nullopt`. The OS icon stays. A set id is loaded in `Engine::init` after the catalogs |
| `world()` | the world `GameBase` created with `Worlds::add` |
| `on_start` / `on_quit` | empty |

`GameBase` takes `Worlds&`. The host runs `Schedule::Fixed` and `Schedule::Frame`. A game adds its systems in `on_start`.

`EngineGame` requires `GameT` to derive from `IGame` and to be constructible from `const EngineServices&`.

`EngineServices` holds references only. `Engine::init` owns the objects.

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
| Android back, text input inactive | `quit()` |
| Android back, text input active | no change. The SDL poll path dismisses the field |

## Platform and user data

`current_platform()` is Web when `__EMSCRIPTEN__`, Android when `__ANDROID__`, otherwise Native.

| Platform | Loop | Graphics profile |
| --- | --- | --- |
| Native | blocking `while` | OpenGL 3.3 Core |
| Web | `requestAnimationFrame` | WebGL2, ES |
| Android | blocking | GLES 3.0 |

`audio_requires_user_gesture()` is true only on Web. `haptics_has_amplitude_control(Platform)` is true only for Android. That predicate does not encode the API 26 runtime check. See [Haptics](Haptics.md).

`user_data_directory(organization, application)` returns a writable directory. The game owns the file format.

| Result | When |
| --- | --- |
| Desktop SDL pref path | `%APPDATA%`, `$XDG_DATA_HOME` or `~/.local/share`, `~/Library/Application Support` |
| Web | `/storage/<org>/<app>/`. `engine_add_sdl3` forces `SDL_EMSCRIPTEN_PERSISTENT_PATH` to `/storage`. The browser may flush a write a few frames later |
| Android | `<internal storage>/user/`. SDL's Android pref path is the storage root and ignores the two name strings. `applicationId` isolates apps |
| `errc::invalid_argument` | empty, spaced, trailing-dot, or Windows-device path segment |
| `errc::function_not_supported` | `ENGINE_WITH_WINDOW` is off |
| `errc::io_error` | SDL could not create the pref directory |

Main thread only. On Android, call it after `Engine::init`.

Assets roots: [Runtime Assets](../build/Runtime%20Assets.md).

## Log and fatal errors

`engine::log::init()` is a null sink for tests. `init(exe_dir)` writes `<exe_dir>/game.log`. `info`, `warn`, and `error` are the public facade. spdlog stays in `src/core/log.cpp`.

`IFatalError::report` is the fatal hook (`include/engine/resources/fatal_error.h`). `SdlFatalError` shows an SDL message box and quits. It is compiled only with `ENGINE_WITH_WINDOW`.

## Always compiled

| File | Role |
| --- | --- |
| `src/core/build_info.cpp` | `build_id()` returns `kBuildId` as compiled into `engine` |
| `src/core/fixed_step.cpp` | accumulator |
| `src/core/host.cpp` | fake-canvas host |
| `src/core/input_system.cpp` | bind table and event enqueue |
| `src/core/log.cpp` | spdlog |
| `src/core/platform.cpp` | paths, staging, `user_data_directory` |
| `src/core/frame_step.cpp` | `flush_worlds`, `simulate_worlds` |
| `src/core/worlds.cpp` | process worlds, window binding, per-world clocks |
| `src/core/game_loop.cpp` | frame clock. Calls `IPresentation`, not SDL |
| `src/core/web_loop.cpp` | `MainLoopPolicy`, `LoopShutdown` |
| `src/core/app_lifecycle.cpp` | pause, resume, terminate, Android back |
| `src/cli/cli_server.cpp` | loopback server. The translation unit is empty without `ENGINE_CLI_SERVER` |

## Only with `ENGINE_WITH_WINDOW`

| File | Role |
| --- | --- |
| `include/engine/core/engine.h` | `Engine<GameT>::init`, `run`, `dispose` |
| `src/core/engine_runtime.cpp` | owns `IPresentation` and `GameLoop` |
| `src/core/engine_instantiate.cpp` | explicit instantiation of `Engine<WindowSmokeGame>` |
| `src/core/sdl_fatal_error.cpp` | message box and quit |
| `src/render/opengl/sdl_gl_presentation.cpp` | SDL init, poll, windows, GL factory |

`IPresentation` (`src/core/presentation.h`) is the private seam `GameLoop` calls: poll, sync, draw, fonts, images, window control.

## Public headers

`include/engine/engine.h` is the umbrella. It also includes `core/engine.h` when `ENGINE_WITH_WINDOW` is set.

- `include/engine/igame.h`
- `include/engine/log.h`
- `include/engine/core/application_state.h`
- `include/engine/core/app_lifecycle.h`
- `include/engine/core/build_info.h`
- `include/engine/core/engine.h`
- `include/engine/core/engine_runtime.h`
- `include/engine/core/engine_services.h`
- `include/engine/core/export.h`
- `include/engine/core/fixed_step.h`
- `include/engine/core/host.h`
- `include/engine/core/input_system.h`
- `include/engine/core/key_code.h`
- `include/engine/core/platform.h`
- `include/engine/core/sdl_fatal_error.h`
- `include/engine/core/time.h`
- `include/engine/core/web_loop.h`
- `include/engine/core/window_desc.h`
- `include/engine/core/window_control.h`
- `include/engine/core/worlds.h`

## Tests

`tests/cmake_sanity_test.cpp`, `tests/host_test.cpp`, `tests/time_test.cpp`, `tests/input_test.cpp`, `tests/log_test.cpp`, `tests/platform_test.cpp`, `tests/web_loop_test.cpp`, `tests/android_lifecycle_test.cpp`, `tests/android_assets_test.cpp`, `tests/window_icon_test.cpp`, `tests/window_style_test.cpp`, `tests/cli_server_test.cpp`, `tests/worlds_test.cpp`.

## See also

- [Runtime Loop](../architecture/Runtime%20Loop.md)
- [Module Map](../architecture/Module%20Map.md)
- [CMake](../build/CMake.md)
