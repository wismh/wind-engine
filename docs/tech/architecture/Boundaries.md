---
tags: [architecture]
---

# Boundaries

Rules that keep games from depending on SDL, glad, or NanoVG.

## Public vs private

| Public `include/engine/` | Private `src/` |
| --- | --- |
| `IGame`, `World`, components, `AssetsDb`, `IMaterial`, UI MVVM, `IAudioSystem`, `IHaptics` | OpenGL classes, NanoVG painter, XML/CSS parsers, `stb_image`, clip/mixer |
| glm types on game-facing structs | spdlog, tinyxml2, tomlplusplus, SDL |

Games include `<engine/…>` only. `IUiPainter` is **not** public ([[src.ui.painter.h]]). Games draw custom UI through [[include.engine.ui.draw_list.h]] `IDrawList` from an `IPaint` on the ViewModel.

## Forbidden ([[architecture/Principles]])

- EnTT as a dependency.
- `onClick`, `CmdCustomDraw`, `EventBus`, scene-graph `Node*` (`NodeEcs` / `NodeUI`). `ui::Node` is the UI builder. Named `IPaint` / `IDrawList` is not `CmdCustomDraw`.
- `#include` glad / SDL in tests (except what already lives behind `ENGINE_WITH_WINDOW` in production code).
- Regenerating [[include.engine.builtin_ids.h|builtin GUIDs]].
- A service locator (`Engine::get_audio()`).
- Win32 / SDL platform types in `include/`.

## Compile flags

| Macro | Meaning |
| --- | --- |
| `ENGINE_WITH_WINDOW` | PUBLIC on `engine`. Unlocks `Engine<GameT>`, OpenGL sources, Boost.DI include path. |
| `ENGINE_WITH_AUDIO` | PRIVATE. Real `MIX_*`; WAV-only mixer in CMake. Same root/subdirectory split as `ENGINE_WITH_WINDOW`: OFF at engine root, ON for a game's subdirectory. |
| `ENGINE_WITH_WEB` | PUBLIC. Web profile helpers; Emscripten turns this on by default. |
| `ENGINE_WITH_ANDROID` | PUBLIC. Android profile helpers; NDK builds turn this on by default. |
| `ENGINE_WITH_GLES` | PUBLIC. ES 3.0 backend (no glad, NanoVG GLES3, shader adapt). Default ON when `EMSCRIPTEN` or `ANDROID`. |
| `ENGINE_BUILD_TESTS` | Builds `engine_tests`; implies `ENGINE_WITH_GTEST`. Default ON at engine root, OFF when Wind is a subdirectory. |
| `ENGINE_WITH_GTEST` | Vendors GoogleTest (`external/googletest`) without building `engine_tests` — set this alone so a game's own test target gets `GTest::gtest_main` without also compiling the engine's internal suite. Defaults to `ENGINE_BUILD_TESTS`. |

`IHaptics` has no `ENGINE_WITH_*` flag of its own — its Native/Web/Android split happens at
compile time via `__EMSCRIPTEN__`/`__ANDROID__` inside `HapticsSystem`, not a build option.

When a game `add_subdirectory`s Wind, window defaults **ON**. Engine-root `vs` preset keeps window **OFF** so CI stays headless. [[build/CMake]].

`engine_tests` links `engine` alone by default; `ENGINE_WITH_WINDOW` additionally links it to
`SDL3::SDL3` and `glad` (both otherwise `PRIVATE` on `engine`, so their include dirs would not
reach a test TU) — needed so a test can `#include "render/opengl/window_system.h"` under the
same `#if defined(ENGINE_WITH_WINDOW)` guard production code uses ([[tests.window_icon_test.cpp]]).

## `engine_tests`

Logic and fakes, not GPU or a mixer device. Cover happy path and the main failure (empty SFX pool, unknown GUID, missing `.meta` at codegen, unknown XML tag, missing binding name).

**Not in `engine_tests`:**

- Pixel-perfect OpenGL / NanoVG screenshots.
- `Engine<GameT>::init` + a real SDL window (needs a display).
- Decoding a WAV through SDL_mixer in CI. Use a fake `Audio` / fake track.
- Gameplay (bot AI, score) — that belongs in the game repo.

Do not `#include` glad in tests. Command execution can record calls instead of drawing.

## Fatal vs warn

- Unknown UI element / missing `{binding}` name: fatal (`IFatalError`).
- Unknown CSS property: warn, continue.
- `AssetsDb::get` miss / type mismatch: fatal hook.
- `try_get`: `std::expected`, no dialog.

## See also

- [[architecture/Overview]]
- [[architecture/Principles]]
- [[architecture/Scope]]
- [[modules/Core]]
- [[include.engine.resources.fatal_error.h]]
