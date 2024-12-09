---
tags: [architecture]
---

# Boundaries

Rules that keep games from depending on SDL, glad, or NanoVG.

## Public vs private

| Public `include/engine/` | Private `src/` |
| --- | --- |
| `IGame`, `World`, components, `AssetsDb`, `IMaterial`, UI MVVM, `IAudioSystem` | OpenGL classes, NanoVG painter, XML/CSS parsers, `stb_image`, clip/mixer |
| glm types on game-facing structs | spdlog, tinyxml2, tomlplusplus, SDL |

Games include `<engine/…>` only. `IUiPainter` is **not** public ([[src.ui.painter.h]]).

## Forbidden (SDD + playbook)

- EnTT as a dependency.
- `onClick`, `CmdCustomDraw`, `EventBus`, `Node*`.
- `#include` glad / SDL in tests (except what already lives behind `ENGINE_WITH_WINDOW` in production code).
- Regenerating [[include.engine.builtin_ids.h|builtin GUIDs]].
- Building the whole engine in one session.

## Compile flags

| Macro | Meaning |
| --- | --- |
| `ENGINE_WITH_WINDOW` | PUBLIC on `engine`. Unlocks `Engine<GameT>`, OpenGL sources, Boost.DI include path. |
| `ENGINE_WITH_AUDIO` | PRIVATE. Real `MIX_*`; WAV-only mixer in CMake. |
| `ENGINE_WITH_WEB` | PUBLIC. Web profile helpers; Emscripten turns this on by default. |
| `ENGINE_WITH_ANDROID` | PUBLIC. Android profile helpers; NDK builds turn this on by default. |
| `ENGINE_WITH_GLES` | PUBLIC. ES 3.0 backend (no glad, NanoVG GLES3, shader adapt). Default ON when `EMSCRIPTEN` or `ANDROID`. |
| `ENGINE_BUILD_TESTS` | `engine_tests` + GoogleTest. Default ON at engine root, OFF when Wind is a subdirectory. |

When a game `add_subdirectory`s Wind, window defaults **ON**. Engine-root `vs` preset keeps window **OFF** so CI stays headless. [[build/CMake]].

## Fatal vs warn

- Unknown UI element / missing `{binding}` name: fatal (`IFatalError`).
- Unknown CSS property: warn, continue.
- `AssetsDb::get` miss / type mismatch: fatal hook.
- `try_get`: `std::expected`, no dialog.

## See also

- [[architecture/Overview]]
- [[modules/Core]]
- [[include.engine.resources.fatal_error.h]]
