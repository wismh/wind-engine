# Boundaries

Rules that keep games from depending on SDL, glad, or NanoVG.

## Public vs private

| Public `include/engine/` | Private `src/` |
| --- | --- |
| `IGame`, `World`, components, `AssetsDb`, `IMaterial`, UI MVVM, `IAudioSystem`, `IHaptics` | OpenGL classes, NanoVG painter, XML and CSS parsers, `stb_image`, `stb_truetype`, clipboard, mixer |
| glm types on game-facing structs | spdlog, tinyxml2, tomlplusplus, SDL |

Games include `<engine/…>` only. `IUiPainter` is not public (`src/ui/painter.h`). Games draw custom UI through `IDrawList` (`include/engine/ui/draw_list.h`) from an `IPaint` on the view-model.

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
| `ENGINE_BUILD_TESTS` | Builds `engine_tests` and implies `ENGINE_WITH_GTEST`. Default ON at the engine root, OFF when Wind is a subdirectory. |
| `ENGINE_WITH_GTEST` | Vendors GoogleTest (`external/googletest`) without building `engine_tests`. A game can set this alone so its own test target gets `GTest::gtest_main`. Defaults to `ENGINE_BUILD_TESTS`. |
| `ENGINE_UI_PROFILER` | `PUBLIC` on `engine` for Debug and RelWithDebInfo. UI profiler scopes and window. Release and MinSizeRel compile the call sites out. The public toggle is an inline no-op. See [UI Profiler](../features/UI Profiler.md). |
| `ENGINE_CLI_SERVER` | `PUBLIC` on `engine` for Debug and RelWithDebInfo, and not on Emscripten or Android. Loopback server for `wind-cli`. Other configurations do not listen and do not write a descriptor. The host tool still builds and does not link `engine`. See [CLI](../features/CLI.md). |

`IHaptics` has no `ENGINE_WITH_*` flag. The Native, Web, and Android split is `#if defined(__EMSCRIPTEN__)` / `__ANDROID__` inside `HapticsSystem`.

When a game `add_subdirectory`s Wind, window defaults ON. The engine-root `vs` preset keeps window OFF so CI stays headless. See [CMake](../build/CMake.md).

`engine_tests` links `engine` alone by default. `ENGINE_WITH_WINDOW` additionally links `SDL3::SDL3` and, unless GLES, `glad`. Both are otherwise `PRIVATE` on `engine`, so their include directories would not reach a test translation unit. That link exists so a test can `#include "render/opengl/window_system.h"` under the same `ENGINE_WITH_WINDOW` guard production code uses (`tests/window_icon_test.cpp`).

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
