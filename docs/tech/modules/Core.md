---
tags: [module]
---

# Core

Host, time, input polling, logging, fatal errors, and (when windowed) `Engine<GameT>` + SDL runtime.

## Capabilities

- Construct services in `Engine::init` and run the main loop ([[features/Init and Loop]]).
- Fixed-step clock and `Time` on `World::ctx` ([[features/Time]]).
- Map SDL scancodes / mouse into ECS events ([[features/Input Mapper]]).
- spdlog facade `engine::log` ([[features/Logging]]).
- `IFatalError` — tests use a recorder; games use SDL message box.
- `IGame::window_icon()` — optional `AssetId`; `Engine<GameT>::init()` resolves it through
  `AssetsDb` (after the catalog + texture/`UiImage` preload loop) and forwards the
  `render::TextureDesc` to `EngineRuntime::set_window_icon()`. Unset (`std::nullopt`) leaves the
  OS/window-manager default icon alone.
- `IGame::splash_screen()` — `SplashScreen{enabled, image, fade_in/hold/fade_out_seconds}`, defaulting to builtin `splash_wind`. The engine does **not** auto-show it. A game calls `ui::show_splash(world, config, image_size, window)` (typically from `on_start`). That spawns two canvases (opaque `FillWindow` backdrop + letterboxed `ScaleWithScreenSize` image) with `SplashTimer`; both despawn when the timer elapses. Ages with `Time::delta_time` (Frame), including while paused.
- `user_data_directory(organization, application)` — writable per-user directory. The game owns the file format and when to write. Desktop uses the SDL pref path (`%APPDATA%`, `$XDG_DATA_HOME` or `~/.local/share`, `~/Library/Application Support`). Web uses `/storage/<org>/<app>/` (IndexedDB); `engine_add_sdl3` sets `SDL_EMSCRIPTEN_PERSISTENT_PATH=/storage` and the browser may flush a write a few frames later. Android uses `<internal storage>/user/` so staged `assets/` stay untouched — SDL's Android pref path is that internal-storage root and ignores the two name strings (`applicationId` isolates apps). Headless builds (`ENGINE_WITH_WINDOW` off) return `errc::function_not_supported`. Main thread only; on Android, after `Engine::init`.

## How it is implemented

**Always compiled** (headless library):

- [[src.core.api_epoch.cpp]] — `kApiEpoch` / `api_epoch()`.
- [[src.core.fixed_step.cpp]] — accumulator.
- [[src.core.host.cpp]] — fake-canvas host for tests (registers systems, ticks).
- [[src.core.input_system.cpp]] — bind table + `handle_key` / `handle_mouse_*` / `handle_touch*`.
- [[src.core.log.cpp]] — spdlog, optional `<exe>/game.log`.
- [[src.core.platform.cpp]] — `Platform` (Native / Web / Android), assets root, graphics/loop profile, APK staging helper, `user_data_directory` (SDL pref path only when `ENGINE_WITH_WINDOW`).
- [[src.core.frame_step.cpp]] — shared `flush_game_events` / `simulate_game_frame` used by `Host` and `GameLoop`.
- [[src.core.game_loop.cpp]] — frame clock and present order. Calls `IPresentation`; no SDL. With `ENGINE_CLI_SERVER`, `begin` starts the loopback server and `end` stops it. Armed clicks run at the start of the frame; `drain` answers after `draw_all`. See [[features/CLI]].
- [[src.cli.cli_server.cpp]] — loopback `POST /exec`. The translation unit is empty without `ENGINE_CLI_SERVER`. See [[features/CLI]].
- [[src.core.web_loop.cpp]] — `MainLoopPolicy` (blocking vs requestAnimationFrame).
- [[src.core.app_lifecycle.cpp]] — pause / resume / terminate / Android back → `ApplicationState`. Back quits only when text input is not active; an active session is the caller's dismiss (`clear_focus` in the SDL poll) and does not quit.

**Only `ENGINE_WITH_WINDOW`:**

- [[include.engine.core.engine.h]] — template Init/Run/Dispose. `GameT` is constructed from `EngineServices`.
- [[src.core.engine_runtime.cpp]] — owns `IPresentation` + `GameLoop`.
- [[src.render.opengl.sdl_gl_presentation.cpp]] — SDL init, poll, windows, OpenGL factory/backend, NanoVG font and image upload.
- [[src.core.engine_instantiate.cpp]] — explicit template / TU glue if any.
- [[src.core.sdl_fatal_error.cpp]] — SDL_ShowSimpleMessageBox + quit.

`Engine.h` is an include-only template so `GameT` is the game class. The game constructor takes `const EngineServices&`.

## Public headers

- [[include.engine.engine.h]] — umbrella + `kApiEpoch`
- [[include.engine.igame.h]]
- [[include.engine.log.h]]
- [[include.engine.core.application_state.h]]
- [[include.engine.core.engine.h]]
- [[include.engine.core.engine_services.h]]
- [[include.engine.core.engine_runtime.h]]
- [[include.engine.core.fixed_step.h]]
- [[include.engine.core.host.h]]
- [[include.engine.core.input_system.h]]
- [[include.engine.core.sdl_fatal_error.h]]
- [[include.engine.core.time.h]]
- [[include.engine.core.platform.h]]
- [[include.engine.core.web_loop.h]]
- [[include.engine.core.app_lifecycle.h]]
- [[include.engine.ui.splash.h]]
- [[include.engine.core.window_desc.h]]
- [[include.engine.core.window_control.h]]

## Tests

[[tests.cmake_sanity_test.cpp]] · [[tests.host_test.cpp]] · [[tests.time_test.cpp]] · [[tests.input_test.cpp]] · [[tests.log_test.cpp]] · [[tests.platform_test.cpp]] · [[tests.web_loop_test.cpp]] · [[tests.android_lifecycle_test.cpp]] · [[tests.android_assets_test.cpp]] · [[tests.window_icon_test.cpp]]

## See also

- [[architecture/Runtime Loop]]
- [[architecture/Module Map]]
- [[features/Windowing]]
- [[build/CMake]]
