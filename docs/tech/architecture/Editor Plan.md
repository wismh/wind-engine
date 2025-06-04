# Editor plan

This is a plan. It is not a description of the engine as it runs today. wind-167 to wind-169 are done: `wind_editor` loads, plays, and stops a game module ([Editor](../features/Editor.md)). The UI Inspector and UI Profiler still open their own windows from the game ([UI Inspector](../features/UI%20Inspector.md), [UI Profiler](../features/UI%20Profiler.md)).

## Goal

The editor becomes the main way to work with Wind. Later it owns the project list, starts the game build, and hosts every tool. The first version is a host executable, `wind_editor`. On start it asks for a built game module (`game.dll`). Play loads that module into the editor process and runs it in `kPrimaryWindow`. Stop unloads it completely. The UI Inspector and UI Profiler live only in the editor window.

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

Every start opens a file dialog for the module. `IWindowControl::request_open_file(owner, filters)` wraps `SDL_ShowOpenFileDialog`. SDL may call back on another thread, so the result is queued and delivered in `poll` as `FileDialogResultEvent` to the owner window's world. No SDL type reaches `include/`.

## Inspector and profiler

The engine keeps the probe and works on any world:

- Inspector: `UiInspector` state in the game world's `ctx`, pick click and hover box on game canvases, element path resolution, and new snapshot functions `inspector_tree`, `inspector_detail`, `inspector_rules`, plus `set_inspector_attached`.
- Profiler: `ProfilerState` rings, `ENGINE_UI_PROFILE` scopes, `profiler_attach` and `profiler_commit_frame`, the CLI capture, plus snapshot functions `profiler_canvases` and `profiler_frames` and `set_ui_profiler_attached`.

Removed from the engine: `InspectorWindowHost`, `ProfilerWindowHost`, `InspectorPanel`, `ProfilerPanel`, `inspector_skips_canvas`, `set_inspector_enabled`, `set_ui_profiler_enabled`, the window-opening part of `sync_*_frames`, the panel documents and models, the window-host lines of the UI installer, and the panel Bind systems.

Moved to `editor/`: panel documents (XML and CSS), view-models, and the profiler chart as an `IPaint` drawn through `IDrawList`. The view-models read the world bound to `kPrimaryWindow` during the editor world's `Phase::Bind`. The Pick toggle writes `UiInspector::pick_pointer` in the game world.

## Tasks

| Task | Content |
| --- | --- |
| wind-166 | Scope and Principles for the editor, this plan |
| wind-167 | `ENGINE_EDITOR`, shared `engine`, `ENGINE_API`, data audit, build id, `vs-editor` preset. `engine_tests` pass static and shared |
| wind-168 | `EngineHost`, `RunHooks`, `ENGINE_GAME`, game module build, `InputSystem::reset`, `IAudioSystem::stop_all`, `AssetsDb::unload_catalog` |
| wind-169 | Module loader, file dialog event, `wind_editor` with Play and Stop, module tests against a fixture module. Done |
| wind-170 | Inspector and profiler split into engine probe and editor panels; feature, module, build, and README pages |

## Done when

- `vs` and `vs-editor` both pass `ctest`. Web and Android presets configure as before.
- A game builds as an executable in its normal preset and as a module in `editor`.
- In the editor: pick a game, Play, inspect and profile it, Stop. The game window is empty and the copy is gone. Rebuild the game with the editor open, Play again, and get the new build. Quitting from the game's menu stops it and leaves the editor running. A module with another build id shows a message, not a crash.

## See also

- [Scope](Scope.md)
- [Principles](Principles.md)
- [Windowing](../features/Windowing.md)
