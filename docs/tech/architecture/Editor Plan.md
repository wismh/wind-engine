# Editor plan

This is a plan. It is not a description of the engine as it runs today. wind-167 to wind-173, wind-176 to wind-179, and wind-181 are done: `wind_editor` loads, plays, inspects, profiles, and stops a game module ([Editor](../features/Editor.md), [UI Inspector](../features/UI%20Inspector.md), [UI Profiler](../features/UI%20Profiler.md)).

## Goal

The editor becomes the main way to work with Wind. Later it owns the project list, starts the game build, and hosts every tool ([Next: projects, launcher, build on Play](#next-projects-launcher-build-on-play)). The first version is a host executable, `wind_editor`. On start it asks for a built game module (`game.dll`). Play loads that module into the editor process and runs it in `kPrimaryWindow`. Stop unloads it completely. The UI Inspector and UI Profiler live only in the editor window.

## Build modes

One CMake switch, `ENGINE_EDITOR`, OFF by default. Presets: `vs-editor` in this repo, `editor` in a game repo.

| | `ENGINE_EDITOR` OFF (exported game) | `ENGINE_EDITOR` ON (editor) |
| --- | --- | --- |
| `engine` | static, as today | shared, `WINDOWS_EXPORT_ALL_SYMBOLS`, PUBLIC `ENGINE_SHARED` |
| `engine_add_game` | executable | shared module, `ENGINE_GAME_MODULE` |
| `wind_editor` | not built | built from `editor/` |

Web and Android stay on the static path.

A game declares its entry once with `ENGINE_GAME(game::Game)` from `<engine/game_entry.h>`. In an exported build it expands to `main` with `Engine<Game>`. Under `ENGINE_GAME_MODULE` it expands to the `extern "C"` exports `wind_create_game`, `wind_destroy_game`, and `wind_game_build_id`.

`<engine/core/export.h>` defines `ENGINE_API`: export while building the shared engine, import when consuming it, empty when static. `WINDOWS_EXPORT_ALL_SYMBOLS` does not export data, so mutable data reachable from public headers moves into `.cpp` behind `ENGINE_API`. A function-local static inside an inline header function is duplicated per module. Example: `next_stylesheet_generation` in `include/engine/ui/stylesheet.h`.

## Build id

Windows reuses the `engine.dll` already loaded in the editor process. The game module was compiled against engine headers and instantiates `World` and `AssetsDb` templates from them, so its layout must match that `engine.dll` exactly. A generated `<engine/build_id.h>` holds a hash of the public headers, the compiler id and version, the configuration, and the PUBLIC compile definitions. The game bakes that constant into `wind_game_build_id`. The editor compares it with `engine::build_id()` and refuses a mismatch with a message in its UI. `kApiEpoch` and `api_epoch()` go away: a game calling `api_epoch()` inside the editor process would always reach the editor's own copy.

## Host

`Engine<GameT>::init` builds every service. That setup moves into a non-template `EngineHost` (`<engine/core/engine_host.h>`): runtime, fatal error, assets, input, audio, haptics, worlds, `EngineSystemDeps`, `services()`, `open_primary`, catalog loading, and `attach_game` (bind `kPrimaryWindow`, enable UI and audio, publish the window size, fit canvases). `Engine<GameT>` and the editor both sit on it.

`GameLoop::run` and `EngineRuntime::run` take `RunHooks { on_start, on_frame_end, on_quit }` instead of `IGame&`.

## Windows

The game owns `kPrimaryWindow`. Engine defaults, input events, and the CLI already assume it. The editor window is a secondary window opened through `IWindowControl::open_window`. In Edit mode `kPrimaryWindow` has no world and is cleared. On Play the editor applies the game's `primary_window()` and `window_icon()`. `transparent` cannot change after creation; the editor ignores it and says so in its status line.

## Play

1. Copy `game.dll` and `game.pdb` into `user_data_directory` under `live/<n>/` and load the copy. The module links with `/PDBALTPATH:<name>.pdb` (from `$<TARGET_PDB_FILE_NAME>`; the Visual Studio generator escapes a literal `%_PDB%`) so a debugger finds the copied PDB and the original stays unlocked. Stale copies are removed when the editor starts.
2. Check the build id. Resolve the three exports.
3. Load the game catalog from `assets/` beside the module. `engine_prepare_runtime` already stages it there.
4. `wind_create_game(services)`, apply the window description, `attach_game`, `on_start`.
5. Attach the inspector and profiler panels to the game world.

## Stop

Nothing built from game code may survive `FreeLibrary`.

1. Detach the editor panels.
2. `on_quit`.
3. Destroy every world except the editor's. A game can add worlds; systems, components, `ctx<T>`, view-models, and commands live in them.
4. Close every window except `kPrimaryWindow` and the editor window. Reset drag region, click-through, and overlay mode on `kPrimaryWindow`.
5. `InputSystem::reset`, `IAudioSystem::stop_all`, `AssetsDb::unload_catalog` for the game catalog, and drop the UI images and fonts cached for `kPrimaryWindow`.
6. `wind_destroy_game`, unload, delete the copy.

As built (wind-169), the `kPrimaryWindow` parts of steps 4 and 5 run right after `on_quit`, in `EngineHost::detach_game`, which also clears that window's command buffer: its `CmdDrawUI` entries point into the game world's documents. See [Editor](../features/Editor.md).

A game quits by setting `application_state().running = false`. While playing, the editor checks that in `on_frame_end`, sets it back, and stops. Closing the editor window quits the editor.

The editor's `engine.dll` stays locked while the editor runs. An engine change needs an editor restart.

## Choosing the game

Every start opens a file dialog for the module. `IWindowControl::request_open_file(owner, filters)` wraps `SDL_ShowOpenFileDialog`. It returns a `FileDialogCall` the editor owns. SDL may call back on another thread, so the answer is queued and becomes visible on the call in `poll`. No SDL type reaches `include/`.

## Inspector and profiler

The engine keeps the probe and works on any world:

- Inspector: `UiInspector` state in the game world's `ctx`, pick click and hover box on game canvases, element path resolution, and snapshot functions `inspector_tree`, `inspector_select`, `inspector_toggle`, `inspector_detail`, `inspector_rules`, plus `set_inspector_attached`.
- Profiler: `ProfilerState` rings, `ENGINE_UI_PROFILE` scopes, `profiler_attach` and `profiler_commit_frame`, the CLI capture, plus snapshot functions `profiler_canvases`, `profiler_frames`, `profiler_shared_frames`, `profiler_select`, `set_profiler_paused`, and `set_ui_profiler_attached`. Scopes record only passes over the attached world, so the editor's own canvases stay out.

Removed from the engine: `InspectorWindowHost`, `ProfilerWindowHost`, `InspectorPanel`, `ProfilerPanel`, `inspector_skips_canvas`, `set_inspector_enabled`, `set_ui_profiler_enabled`, the window-opening part of `sync_*_frames`, the panel documents and models, the window-host lines of the UI installer, and the panel Bind systems.

Moved to `editor/`: panel documents (XML and CSS), view-models, and the profiler chart as an `IPaint` drawn through `IDrawList`. The view-models read the world bound to `kPrimaryWindow` during the editor world's `Phase::Game`, before its Bind, so the same frame's bindings see them. The Pick toggle writes `UiInspector::pick_pointer` in the game world; pick starts off so the game gets its clicks.

## Next: one editor, Release SDK, DebugGame

Today `ENGINE_EDITOR` in a game repo builds a second `engine.dll` and a second `wind_editor` inside the game's build tree. The target is one editor build for every project, shipped as an SDK, built in Release, and games that load into it in `DebugGame` or `Release`.

### SDK

`cmake --preset vs-editor`, `cmake --build build-editor --config Release`, then `cmake --install build-editor --config Release --prefix out/sdk` (`/out/` is gitignored). The SDK holds one configuration:

| Path | Content |
| --- | --- |
| `bin/` | `wind_editor.exe`, `engine.dll`, `asset_codegen.exe`, `icon_codegen.exe`, their `.pdb`, `assets/engine/`, `assets/editor/` |
| `lib/` | `engine.lib` |
| `include/` | `engine/**` public headers, the generated `engine/build_id.h`, glm headers |
| `cmake/` | `wind_sdk.cmake` (imported targets) and `wind_game.cmake` (game functions) |

`wind_sdk.cmake` records `WIND_SDK_CONFIG` (Release) and defines imported targets: `engine` (shared, `INTERFACE_COMPILE_DEFINITIONS` fixed to the SDK's: `ENGINE_EDITOR`, `ENGINE_SHARED`, `ENGINE_WITH_WINDOW`, `ENGINE_UI_PROFILER`, `ENGINE_CLI_SERVER`), `glm::glm`, `asset_codegen`, `icon_codegen`. As built (wind-173) every configuration of the editor build carries all five, and the SDK's `bin/` has the `.pdb` files.

`engine_add_game`, `engine_configure_app`, `engine_prepare_runtime` and their helpers move from the root `CMakeLists.txt` into `cmake/wind_game.cmake`. The source build and the SDK include the same file.

### A game in editor mode

The game's `CMakeLists.txt` does not change: it still calls `add_subdirectory(external/engine)`. When the cache variable `WIND_EDITOR_SDK` is set, the engine's root `CMakeLists.txt` includes `${WIND_EDITOR_SDK}/cmake/wind_sdk.cmake` and returns. It compiles no engine, no tools, and no editor. It still adds googletest from the submodule when `ENGINE_WITH_GTEST` is ON, so game tests build. Headers, `build_id.h`, `engine.lib`, and the game functions all come from the SDK, so the game's `kBuildId` is the SDK's by construction.

As built since wind-176 the game has no submodule and calls `find_package(Wind)`; `WIND_EDITOR_SDK` is gone ([Next: projects, launcher, build on Play](#next-projects-launcher-build-on-play)).

For now the path is set by hand per machine, in the game's `CMakeUserPresets.json` (not committed) on top of a committed `editor` preset. Later the editor starts the build and passes it.

`engine_add_game` in SDK mode builds the module only. No `engine.dll` copy, no `assets/engine/` copy, no `wind_editor`. The module still lands in `bin/<config>/` with `assets/catalog.toml` and the game's assets beside it.

`ENGINE_EDITOR` as a CMake option in a game's tree (without `WIND_EDITOR_SDK`) becomes a configure error that points at the SDK. The engine repo's own `vs-editor` build keeps it: that is the build that makes the SDK, runs `engine_tests` and `wind_editor_tests` on the shared engine, and builds the fixture modules.

### `ENGINE_EDITOR` define

`ENGINE_EDITOR` is also a `PUBLIC` compile definition of `engine` in the editor build, so it reaches the editor and every game module through the `engine` target.

- `ENGINE_UI_PROFILER` and `ENGINE_CLI_SERVER` are on in Debug, RelWithDebInfo, and any configuration with `ENGINE_EDITOR`. A Release editor has the Profiler tab and `wind-cli`.
- An exported game has no `ENGINE_EDITOR`. Its Release still compiles the profiler out.
- Game code may test `ENGINE_EDITOR` only for tools. Gameplay must not depend on it, or the game in the editor behaves unlike the exported game. This rule goes into Principles.

### Release editor with symbols

Under `ENGINE_EDITOR` the Release configuration of `engine`, `wind_editor`, and the tools also gets `/Zi` and `/DEBUG /OPT:REF /OPT:ICF`. Same code as Release (`/O2 /Ob2`), plus `.pdb` files for call stacks, breakpoints, and crash dumps. The SDK installs them.

### DebugGame

A game in SDK mode gets two configurations, `DebugGame` and `Release` (`CMAKE_CONFIGURATION_TYPES` in the `editor` preset). Both link the Release SDK. As built, `engine_sdk_configurations` (`cmake/wind_game.cmake`) fails the configure for any other configuration, and sets the `DebugGame` flags as cache defaults so every target of the game's tree (its tests and googletest too) gets them.

| | `DebugGame` | `Release` |
| --- | --- | --- |
| Game code | `/Od /Ob0 /Zi /RTC1`, no `NDEBUG` | `/O2 /Zi`, `NDEBUG` |
| CRT | `/MD` (`MSVC_RUNTIME_LIBRARY` MultiThreadedDLL) | `/MD` |
| `_ITERATOR_DEBUG_LEVEL` | 0 | 0 |

`engine_add_game` sets the CRT and the Release extras per target, because a normal variable set inside `external/engine` does not reach the game's directory. The `DebugGame` flags are cache entries, which do. Game asserts and `#if !defined(NDEBUG)` work in `DebugGame`. STL checks and the CRT debug heap need `/MDd`, which cannot load into a Release `engine.dll`; they stay out.

Guard: under `ENGINE_GAME_MODULE`, `<engine/game_entry.h>` fails to compile on MSVC when `_DEBUG` or `_ITERATOR_DEBUG_LEVEL` differs from the engine's. As built, the engine's values are baked into the SDK's `<engine/build_id.h>` (`ENGINE_BUILD_DEBUG_CRT`, `ENGINE_BUILD_ITERATOR_DEBUG_LEVEL`), so a Debug SDK expects the debug CRT without a special case. A wrong CRT is a compile error, not a crash on Play.

Debugging the engine itself: install a Debug SDK (`--config Debug --prefix out/sdk-debug`). `wind_sdk.cmake` then reports `WIND_SDK_CONFIG` Debug, and `engine_add_game` gives the game one configuration, `Debug`. `engine_tests` in Debug stays the main tool.

### Next tasks

| Task | Content |
| --- | --- |
| wind-172 | `cmake/wind_game.cmake`, install rules and `wind_sdk.cmake`, SDK mode behind `WIND_EDITOR_SDK`, `ENGINE_EDITOR` in a game tree without the SDK is an error. Verified with the scratch smoke game against `out/sdk`. Done |
| wind-173 | `ENGINE_EDITOR` define, profiler and CLI on in the editor's Release, Release with `.pdb`, `DebugGame` and `Release` for games in SDK mode, CRT guard in `game_entry.h`, the Principles rule. Verified: Release editor from the SDK plays a `DebugGame` module with the Profiler tab working. Done |

Game repos (`tic-tac-toe`, `electromagnetic-field`) were to switch their `editor` preset to `WIND_EDITOR_SDK` after wind-173; wind-176 replaced that with `find_package(Wind)`.

## Next: projects, launcher, build on Play

The editor is the only way to work with Wind. A game repo stops carrying the engine as a submodule: it finds an installed SDK, and that SDK's version is the game's engine version. A launcher lists projects and installed SDKs and starts the matching editor with the project. Play builds the game module and loads it. Export (standalone executable, Web, Android) is out of this plan.

### One engine version, held by the SDK

- `project(engine VERSION x.y.z)` in the engine's `CMakeLists.txt` is the engine version, semver. A release is a commit on `main` with that version and a `vx.y.z` tag, so an old SDK can be built again from its tag.
- `cmake --install` writes `<sdk>/sdk.toml`: `version`, `commit`, `dirty` (uncommitted changes in the engine checkout), `config`, and `build_id`. It is the one file the launcher and the editor read to know an SDK; its keys only grow.
- The build id stays the last check on Play.

### A game finds the SDK

The game's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_game CXX)
find_package(Wind REQUIRED)
engine_add_game(my_game src/main.cpp src/game.cpp)
```

`<sdk>/cmake/WindConfig.cmake` is the old `wind_sdk.cmake`: imported `engine`, `glm::glm`, `asset_codegen`, `icon_codegen`, and now `GTest::gtest` and `GTest::gtest_main`, then the game functions and the configuration check. `WindConfigVersion.cmake` accepts only the exact version. The SDK is found through `CMAKE_PREFIX_PATH=<sdk>`: the editor passes it when it configures (wind-178); until then the game's `CMakeUserPresets.json` sets it. `WIND_EDITOR_SDK` and the SDK branch of the engine's root `CMakeLists.txt` go away.

GoogleTest is built in the editor build with the SDK's CRT and installed into the SDK (`include/gtest/`, `lib/`, with the compile `.pdb` beside the libraries), so a game's tests need no submodule.

### Engine source in the SDK

`<sdk>/source/` holds the engine's build input: `CMakeLists.txt`, `cmake/`, `include/`, `src/`, `tools/`, `builtin_assets/`, and `external/` without git metadata. The editor does not use it. It is there so export can build the static engine from the exact version of the SDK, with `add_subdirectory(<sdk>/source wind)`; until export exists that line is also the manual way to build a standalone executable.

### Child processes

The editor and the launcher are engine clients and include only `<engine/...>`, so starting `cmake` or `wind_editor` is an engine API. `ProcessCall`, owned by the caller like `HttpCall` and `FileDialogCall`: executable, arguments, working directory, extra environment; output lines and the exit code become visible in `poll`. A detached start (the launcher starting an editor) has no call. Windows first; other platforms report `Unsupported`.

### Project

`wind_project.toml` in the game repo root marks a Wind project: `name`, `engine` (the version), `target` (the `engine_add_game` target). `wind_editor --project <dir>` replaces `--game`:

1. Read `wind_project.toml`. An `engine` other than the editor's own `sdk.toml` version is refused with both versions.
2. Play: configure `<dir>/build-editor` when it has no cache (`-DCMAKE_PREFIX_PATH=<own sdk>`, `-DCMAKE_CONFIGURATION_TYPES=DebugGame;Release`), then `cmake --build ... --config DebugGame --target <target>`, both through `ProcessCall`. The frame keeps running; the Build panel shows the output (`VSLANG=1033`, so MSBuild writes English). A failed build does not start the game.
3. The module path comes from the CMake File API (codemodel), not a guessed `bin/<config>/` path. Then Play continues as today.

As built (wind-178): the configure also passes `-DWind_DIR=<sdk>/cmake`, and runs when the cache's `Wind_DIR` is not this SDK (a cache from another SDK too). The module path is a record `engine_add_game` writes with `file(GENERATE)` (`<build>/wind/<target>.<config>.module`), not the File API: the engine has no JSON reader, and the record is exact. `wind_project.toml` and `sdk.toml` are read by the engine's [Project](../modules/Project.md) module, which the launcher will share. A bound `scroll-y` is now clamped before ItemsControl virtualization, so the Build log can ask for its end. See [Editor](../features/Editor.md#build).

Building while the game plays is allowed: the editor runs a copy from `live/<n>/`.

### Launcher

`wind_launcher` is a standalone Wind app built statically, not against any `engine.dll`, so it serves every SDK version. It keeps its list in `user_data_directory("Wind", "Launcher")`.

- Projects: recent list, Add (folder dialog, `SDL_ShowOpenFolderDialog` behind `IWindowControl`), Open. Open starts the SDK whose `version` matches the project's `engine` with `--project`. No match: the project shows the version it needs.
- SDKs: scanned from one install directory, plus Locate for a dev SDK such as `out/sdk`.
- Later: new project from a template, download of SDK releases.

As built (wind-179): Add picks the project's `wind_project.toml` through the existing open-file dialog instead of a new folder dialog, and Locate picks an SDK's `sdk.toml`. Installed SDKs are `%LOCALAPPDATA%/Programs/Wind/Sdks/*` (wind-183), on an SDKs page beside Projects with a per-row menu to forget a located SDK or delete an installed one. New project (wind-183) copies the SDK's `templates/empty/` with a name, a location (`request_open_folder`), and the SDK picked, then opens it. The preset is `vs-launcher` (`ENGINE_LAUNCHER`). See [Launcher](../features/Launcher.md).

### Tasks

| Task | Content |
| --- | --- |
| wind-176 | Engine version, `sdk.toml`, `WindConfig.cmake` and `find_package(Wind)`, GoogleTest and the engine source in the SDK, `WIND_EDITOR_SDK` removed. Verified with a smoke game against an installed SDK: module and tests in `DebugGame` and `Release`, Play in the SDK's editor, `find_package(Wind 0.2.0)` refused, and a static executable with `add_subdirectory(<sdk>/source)`. Done |
| wind-177 | `ProcessCall` in the engine (Windows) and tests. As built: `IProcessLauncher` in `EngineServices::processes`, polled after HTTP; a job object per child, so cancel and the child's exit end everything it started ([Process](../modules/Process.md)). Done |
| wind-178 | `wind_project.toml`, `wind_editor --project`, configure and build on Play, Build panel. Done |
| wind-179 | `wind_launcher`: projects, SDKs, Open. Done |
| wind-181 | `wind-cli` drives the editor: `launch <project> [--play --wait]`, `state`, `play [--wait]`, `stop`, `open`; descriptor `kind`; UI commands reach the world of their window and answer at once when it has none; `RunHooks::cli` for a host's own commands; `wind-cli` in the SDK's `bin/` ([CLI](../features/CLI.md#editor-commands)). Done |

Game repos drop the `external/engine` submodule for `find_package(Wind)` in their own change, after wind-176.

## Tasks

| Task | Content |
| --- | --- |
| wind-166 | Scope and Principles for the editor, this plan |
| wind-167 | `ENGINE_EDITOR`, shared `engine`, `ENGINE_API`, data audit, build id, `vs-editor` preset. `engine_tests` pass static and shared |
| wind-168 | `EngineHost`, `RunHooks`, `ENGINE_GAME`, game module build, `InputSystem::reset`, `IAudioSystem::stop_all`, `AssetsDb::unload_catalog` |
| wind-169 | Module loader, file dialog event, `wind_editor` with Play and Stop, module tests against a fixture module. Done |
| wind-170 | Inspector and profiler split into engine probe and editor panels; feature, module, build, and README pages. Done |

## Done when

- `vs` and `vs-editor` both pass `ctest`. Web and Android presets configure as before.
- A game builds as an executable in its normal preset and as a module in `editor`.
- In the editor: pick a game, Play, inspect and profile it, Stop. The game window is empty and the copy is gone. Rebuild the game with the editor open, Play again, and get the new build. Quitting from the game's menu stops it and leaves the editor running. A module with another build id shows a message, not a crash.

## See also

- [Scope](Scope.md)
- [Principles](Principles.md)
- [Windowing](../features/Windowing.md)
