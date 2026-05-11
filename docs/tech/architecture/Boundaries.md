# Boundaries

Rules that keep games from depending on SDL, glad, or NanoVG.

## Public vs private

| Public `include/engine/` | Private `src/` |
| --- | --- |
| `IGame`, `World`, components, `AssetsDb`, `IMaterial`, UI MVVM, `IAudioSystem`, `IHaptics`, `IHttpClient`, `IProcessLauncher` | OpenGL classes, NanoVG painter, XML and CSS parsers, `stb_image`, `stb_truetype`, clipboard, mixer |
| glm types on game-facing structs | spdlog, tinyxml2, tomlplusplus, SDL |

Games include `<engine/…>` only. `IUiPainter` is not public (`src/ui/painter.h`). Games draw custom UI through `IDrawList` (`include/engine/ui/draw_list.h`) from an `IPaint` on the view-model.

A public header holds no mutable state. No function-local `static` in an inline or template function, no non-`constexpr` static data member, no mutable `inline` variable. In the editor build each module would get its own copy, and `WINDOWS_EXPORT_ALL_SYMBOLS` does not export data. Put the state in a `.cpp` and declare the accessor with `ENGINE_API`. Example: `ui::next_stylesheet_generation` (`src/ui/stylesheet.cpp`).

## Forbidden

See [Principles](Principles.md).

- `onClick`, `CmdCustomDraw`, `EventBus`, scene-graph `Node*` (`NodeEcs` / `NodeUI`). `ui::Node` is the UI builder. Named `IPaint` / `IDrawList` is not `CmdCustomDraw`.
- `#include` of glad or SDL from tests, except the production code already behind `ENGINE_WITH_WINDOW`.
- Regenerating builtin GUIDs in `include/engine/builtin_ids.h`.
- A service locator (`Engine::get_audio()`).
- Win32 or SDL platform types in `include/`.

## Compile flags

| Macro | Meaning |
| --- | --- |
| `ENGINE_WITH_WINDOW` | `PUBLIC` on `engine`. Unlocks `Engine<GameT>` and the OpenGL sources. |
| `ENGINE_WITH_AUDIO` | `PRIVATE`. Real `MIX_*`. WAV-only mixer in CMake. Same root and subdirectory split as `ENGINE_WITH_WINDOW`: OFF at the engine root, ON when Wind is `add_subdirectory`'d. |
| `ENGINE_WITH_WEB` | `PUBLIC`. Web profile helpers. Emscripten turns the option ON by default. |
| `ENGINE_WITH_ANDROID` | `PUBLIC`. Android profile helpers. The NDK turns the option ON by default. |
| `ENGINE_WITH_GLES` | `PUBLIC`. ES 3.0 backend (no glad, NanoVG GLES3, shader adapt). Default ON when `EMSCRIPTEN` or `ANDROID`. |
| `ENGINE_EDITOR` | CMake option, OFF by default. Builds `engine` as a shared library with `WINDOWS_EXPORT_ALL_SYMBOLS`. Needs `ENGINE_WITH_WINDOW`. Configure fails on Emscripten and Android. Also a `PUBLIC` define on `engine` (`ENGINE_EDITOR=1`), so `wind_editor`, the tests, the fixture modules, and every game module built against the SDK see it. Game code may test it only for tools, never for gameplay ([Principles](Principles.md)). |
| `ENGINE_SHARED` | `PUBLIC` on `engine` when `ENGINE_EDITOR` is ON. With it, `ENGINE_API` (`include/engine/core/export.h`) imports on Windows and sets default visibility elsewhere. Without it the macro is empty. |
| `ENGINE_BUILDING` | `PRIVATE` on `engine` when `ENGINE_EDITOR` is ON. `ENGINE_API` exports while the engine itself compiles. |
| `ENGINE_BUILD_TESTS` | Builds `engine_tests` and implies `ENGINE_WITH_GTEST`. Default ON at the engine root, OFF when Wind is a subdirectory. |
| `ENGINE_WITH_GTEST` | Vendors GoogleTest (`external/googletest`) without building `engine_tests`. A game can set this alone so its own test target gets `GTest::gtest_main`. Defaults to `ENGINE_BUILD_TESTS`. |
| `ENGINE_UI_PROFILER` | `PUBLIC` on `engine` for Debug, RelWithDebInfo, and every configuration with `ENGINE_EDITOR` (the Release editor and SDK have it). UI profiler scopes and rings. An exported game's Release and MinSizeRel compile the call sites out: the public functions are inline no-ops and `kUiProfilerBuilt` is false. `wind_editor` does not compile without it. See [UI Profiler](../features/UI%20Profiler.md). |
| `ENGINE_CLI_SERVER` | `PUBLIC` on `engine` for the same configurations as `ENGINE_UI_PROFILER`, and not on Emscripten or Android. Loopback server for `wind-cli`. Other configurations do not listen and do not write a descriptor. The host tool still builds and does not link `engine`. See [CLI](../features/CLI.md). |
| `ENGINE_BUILD_DEBUG_CRT`, `ENGINE_BUILD_ITERATOR_DEBUG_LEVEL` | Macros in the generated `<engine/build_id.h>`: the MSVC C runtime of that engine build (1 and 2 with `/MDd`, 0 and 0 with `/MD`). `<engine/game_entry.h>` fails a game module's compile when its `_DEBUG` or `_ITERATOR_DEBUG_LEVEL` differs. See [CMake](../build/CMake.md#crt-guard). |

`IHaptics` has no `ENGINE_WITH_*` flag. The Native, Web, and Android split is `#if defined(__EMSCRIPTEN__)` / `__ANDROID__` inside `HapticsSystem`. `IHttpClient` follows the same rule: `_WIN32`, `__ANDROID__`, and `__EMSCRIPTEN__` inside `HttpClient`, and WinHTTP, JNI, and `emscripten/fetch.h` stay in `src/net/`. `IProcessLauncher` too: `_WIN32` inside `ProcessLauncher`, and `windows.h` stays in `src/process/windows_process.cpp`.

When a game `add_subdirectory`s Wind, window defaults ON. The engine-root `vs` preset keeps window OFF so CI stays headless. See [CMake](../build/CMake.md).

`engine_tests` links `engine` alone by default. `ENGINE_WITH_WINDOW` additionally links `SDL3::SDL3` and, unless GLES, `glad`. Both are otherwise `PRIVATE` on `engine`, so their include directories would not reach a test translation unit. That link exists so a test can `#include "render/opengl/window_system.h"` under the same `ENGINE_WITH_WINDOW` guard production code uses (`tests/window_icon_test.cpp`). Under `ENGINE_EDITOR` that SDL3 and glad are a second static copy in `engine_tests.exe`, beside the one inside `engine.dll`.

## `engine_tests`

Logic and fakes, not a GPU or a mixer device. Cover the happy path and the main failure (empty SFX pool, unknown GUID, missing `.meta` at codegen, unknown XML tag, missing binding name).

Not in `engine_tests`:

- Pixel-perfect OpenGL or NanoVG screenshots.
- `Engine<GameT>::init` plus a real SDL window (needs a display).
- Decoding a WAV through SDL_mixer in CI. Use a fake `Audio` or fake track.
- Gameplay (bot AI, score). That belongs in the game repo.

Do not `#include` glad in tests. Command execution can record calls instead of drawing.

`Host` is the headless stand-in: it registers engine systems, binds `kPrimaryWindow` to the game world, calls `on_start`, and ticks `flush_worlds` plus `simulate_worlds` against a fake `ICanvas`. It does not poll SDL.

## Fatal vs warn

| Case | Result |
| --- | --- |
| Unknown UI element, missing `{binding}` name | Fatal (`IFatalError`) |
| Unknown CSS property | Warn, continue |
| `AssetsDb::get` miss or type mismatch | Fatal hook |
| `try_get` | `std::expected<…, AssetError>`, no dialog |

## See also

- [Overview](Overview.md)
- [Principles](Principles.md)
- [Scope](Scope.md)
- [Core](../modules/Core.md)
