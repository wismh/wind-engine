---
tags: [module]
---

# Core

Host, time, input polling, logging, fatal errors, and (when windowed) `Engine<GameT>` + SDL runtime.

## Capabilities

- Construct the DI graph and run the main loop ([[features/Init and Loop]]).
- Fixed-step clock and `Time` on `World::ctx` ([[features/Time]]).
- Map SDL scancodes / mouse into ECS events ([[features/Input Mapper]]).
- spdlog facade `engine::log` ([[features/Logging]]).
- `IFatalError` — tests use a recorder; games use SDL message box.
- `IGame::window_icon()` — optional `AssetId`; `Engine<GameT>::init()` resolves it through
  `AssetsDb` (after the catalog + texture/`UiImage` preload loop) and forwards the
  `render::TextureDesc` to `EngineRuntime::set_window_icon()`. Unset (`std::nullopt`) leaves the
  OS/window-manager default icon alone.

## How it is implemented

**Always compiled** (headless library):

- [[src.core.api_epoch.cpp]] — `kApiEpoch` / `api_epoch()`.
- [[src.core.fixed_step.cpp]] — accumulator.
- [[src.core.host.cpp]] — fake-canvas host for tests (registers systems, ticks).
- [[src.core.input_system.cpp]] — bind table + `handle_key` / `handle_mouse_*` / `handle_touch*`.
- [[src.core.log.cpp]] — spdlog, optional `<exe>/game.log`.
- [[src.core.platform.cpp]] — `Platform` (Native / Web / Android), assets root, graphics/loop profile, APK staging helper.
- [[src.core.web_loop.cpp]] — `MainLoopPolicy` (blocking vs requestAnimationFrame).
- [[src.core.app_lifecycle.cpp]] — pause / resume / terminate / Android back → `ApplicationState`.

**Only `ENGINE_WITH_WINDOW`:**

- [[include.engine.core.engine.h]] — template Init/Run/Dispose.
- [[src.core.engine_runtime.cpp]] — SDL init, window, poll, loop, `set_window_icon()` thin wrapper over `WindowSystem::set_icon`.
- [[src.core.engine_instantiate.cpp]] — explicit template / TU glue if any.
- [[src.core.sdl_fatal_error.cpp]] — SDL_ShowSimpleMessageBox + quit.

`Engine.h` is an include-only template so `GameT` is the game class. Boost.DI headers are on the **public** include path when window is on (games do not include DI themselves).

## Public headers

- [[include.engine.engine.h]] — umbrella + `kApiEpoch`
- [[include.engine.igame.h]]
- [[include.engine.log.h]]
- [[include.engine.core.application_state.h]]
- [[include.engine.core.engine.h]]
- [[include.engine.core.engine_runtime.h]]
- [[include.engine.core.fixed_step.h]]
- [[include.engine.core.host.h]]
- [[include.engine.core.input_system.h]]
- [[include.engine.core.sdl_fatal_error.h]]
- [[include.engine.core.time.h]]
- [[include.engine.core.platform.h]]
- [[include.engine.core.web_loop.h]]
- [[include.engine.core.app_lifecycle.h]]

## Tests

[[tests.cmake_sanity_test.cpp]] · [[tests.host_test.cpp]] · [[tests.time_test.cpp]] · [[tests.input_test.cpp]] · [[tests.log_test.cpp]] · [[tests.platform_test.cpp]] · [[tests.web_loop_test.cpp]] · [[tests.android_lifecycle_test.cpp]] · [[tests.android_assets_test.cpp]] · [[tests.window_icon_test.cpp]]

## See also

- [[architecture/Runtime Loop]]
- [[architecture/Module Map]]
- [[build/CMake]]
