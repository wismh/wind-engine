# Editor

`wind_editor` is the editor host. It is built only in the engine repo with `ENGINE_EDITOR` (preset `vs-editor`), from `editor/`, and shipped to games as the editor SDK. It is an engine client like a game: it includes only `<engine/...>` and links the shared `engine`. It opens a project, builds its game module with CMake against its own SDK, plays it in `kPrimaryWindow`, inspects and profiles its UI, and stops it. The plan is [Editor Plan](../architecture/Editor%20Plan.md).

## Get the editor

From the engine repo:

```bash
cmake --preset vs-editor
cmake --build build-editor --config Release
cmake --install build-editor --config Release --prefix out/sdk
out/sdk/bin/wind_editor.exe --project path/to/my_game --play
```

Or from a terminal or an agent, with `wind-cli` from the same `bin/` ([wind-cli](#wind-cli)):

```bash
out/sdk/bin/wind-cli.exe launch path/to/my_game --play --wait
```

The SDK's `bin/` holds `wind_editor.exe`, `wind-cli.exe`, `engine.dll`, their `.pdb` files, `assets/engine/`, and `assets/editor/`, so it runs from there. The Release editor is the normal one: optimized, with symbols, and with the Profiler tab and the `wind-cli` server, because `ENGINE_EDITOR` turns `ENGINE_UI_PROFILER` and `ENGINE_CLI_SERVER` on in every configuration ([CMake](../build/CMake.md#editor-build)). Games load into it in `DebugGame` or `Release` ([Game Consumer](../build/Game%20Consumer.md#debugging-game-code) has the debugger setup). The editor writes its `game.log` beside itself. It builds a project's module against the same SDK (`find_package(Wind)`, [Game Consumer](../build/Game%20Consumer.md#editor-module)), so the build ids match. Only an installed SDK can build projects: the editor finds its SDK as the parent of its `bin/` and reads `sdk.toml` there ([Project](../modules/Project.md)); `build-editor/bin/<config>/wind_editor.exe` has none and says so. Layout: [CMake](../build/CMake.md#editor-sdk).

## Start

`EditorApp::start` (`editor/src/editor_app.cpp`):

1. `EngineHost::init`. Without `--project` the editor reports "No project to open. Start the editor from the Wind launcher, or run wind_editor --project <dir>." through `IFatalError` (a message box) and exits with 1: a project opens only from the [Launcher](Launcher.md), the command line, or `wind-cli open`. Then `open_primary` with `WindowDesc{"Game", 800x600}`. That is `kPrimaryWindow`, the game's window.
2. `load_catalog(assets_root() / "editor")`: the editor's own `catalog.toml`. A failure is fatal.
3. `Worlds::add` for the editor world, `open_window({"Wind Editor", 1280x800})`, `bind_window`, `enable_ui`, and one `UiCanvas` (`FillWindow`) with `assets/ui/editor.xml` and `assets/css/editor.css` over `EditorViewModel`. `EditorPanels` spawns four panel canvases (`Fixed`): `assets/ui/explorer.xml`, `assets/ui/inspector.xml`, `assets/ui/profiler.xml`, and `assets/ui/build.xml`, all with `assets/css/panels.css`, and one `DockSpace` (order 1) that places them, with the saved layout ([Panels](#panels)).
4. `purge_game_module_copies(<user data>/live)`. The user data directory is `user_data_directory("Wind", "Editor")`, read once at start; it also holds `dock_layout.toml` ([Layout file](#layout-file)). Without one the copies go to `<temp>/wind_editor/live` and the layout is not kept (logged).
5. `PlaySession` and `ProjectBuild` over `EngineServices::processes`. The SDK root is `<assets root>/../..`; `read_sdk_manifest` there gives the version and configuration. A missing or bad `sdk.toml` is logged and leaves the editor unable to build.
6. Opens the `--project` directory. Only the panel layout is remembered between runs.

Opening a project reads `<dir>/wind_project.toml` (`read_wind_project`). A read error, a missing SDK, or an `engine` version other than the SDK's `version` shows in the status line and leaves Play disabled: "The project needs engine 0.2.0; this editor is 0.1.0."

Command line:

| Option | Effect |
| --- | --- |
| `--project <dir>` | Open this project (the directory with `wind_project.toml`). Required |
| `--play` | Press Play when the loop starts: build, then play. Needs `--project` |

## Window

A toolbar, and the panels' dock space below it ([Panels](#panels)).

| Control | Binding | Does |
| --- | --- | --- |
| Play / Cancel / Stop | `togglePlay`, `playLabel`, `isPlaying` (`checked`, red while building or playing) | Idle: build and play. Building: cancel the build. Playing: stop. Disabled with no playable project |
| Status line | `statusText` | Ready, Building, Playing, Stopped, the game quit, or why opening, building, or Play failed |
| Project line | `projectText` | The project's name and directory |

The commands are `MethodCommand` (`editor/src/method_command.h`) bound to `Toolbar` methods. Play/Stop only records an `EditorRequest` (`wind-cli` `play` and `stop` record the same one); `Toolbar::show_state` (`RunState` Idle, Building, Playing) sets what Play/Stop does. The editor window's `WindowCloseRequestedEvent` is read by one editor-world system that also only records. Every transition runs in `RunHooks::on_frame_end`, after the frame drew, because Play and Stop create and destroy worlds that no system of that frame may still be walking.

## wind-cli

`EditorApp::run` passes `RunHooks::cli` with `kind` `editor`, so the descriptor says this process is an editor, and a `handle` that calls `EditorCli` (`editor/src/editor_cli.cpp`) with `EditorFacts` read from `EditorApp` (`facts()`: the project name when one is open and playable, its directory, the SDK version). The commands and their replies are in [CLI](CLI.md#editor-commands).

`EditorCli` reads the toolbar (`Toolbar::state`, `playable`, `status`) and acts only through it, so a command and a button do the same thing:

- `play` calls `Toolbar::toggle_play` when Idle and playable: `EditorRequest::Play`.
- `stop` calls it when Building or Playing: `EditorRequest::Stop`, which cancels the build or stops the game.
- `open` keeps the directory; `on_frame_end` takes it (`take_open`) and calls `open_project` before it reads the toolbar request, so a Play in the same frame builds the project just opened.

`handle` runs inside `cli::drain`, after the frame drew and before `on_frame_end`, which then makes the transition in the same frame. A UI command (`tree`, `click`, ...) reaches the game world while playing; between plays `kPrimaryWindow` has no world and the answer is `no world on window 0` at once. `--window` with the editor window's id reaches the editor's own canvases.

## Panels

`EditorPanels` (`editor/src/editor_panels.cpp`) owns `ExplorerPanel`, `InspectorPanel`, `ProfilerPanel`, and `BuildPanel`, their canvases, and one engine `DockSpace` ([Docking](Docking.md#host)) on the editor window. The dock space fills the window below the 56px toolbar (`EditorPanels::kToolbarHeight`, `.toolbar` of `editor.css`), from `order` 1 (`kDockOrder`, above the editor's own canvas). The engine's dock systems draw the tab strips, splitters, floats, and drop preview, handle the pointer on them, and write each panel canvas's rect, order, and window; an inactive tab is `Fixed` with an empty rect, so it takes no clicks. Tabs are as wide as their titles ([Tab width](Docking.md#tab-width)).

| Key | Title | Canvas |
| --- | --- | --- |
| `project` | Project | `explorer.xml` |
| `inspector` | Inspector | `inspector.xml` |
| `profiler` | Profiler | `profiler.xml` |
| `build` | Build | `build.xml` |

`EditorPanels::default_layout()`: Project on the left (25% of the width, full height); on the right, Inspector and Profiler as tabs (Inspector shown) over Build (Build 30% of that column). No panel is closable yet: there is nothing to reopen one with. Tabs can be reordered, split, tabbed together, and floated. The dock space is `DockFloatMode::OsWindow`: a panel dropped outside the dock area or Shift-dragged opens in an OS window of its own, bound to the editor's world, titled with its active tab, moved and resized by the OS, a tool window of the editor window (above it, minimized with it, no taskbar entry); tabs drag between the editor window and those windows, and a float window's close button docks its panels back ([OS window floats](Docking.md#os-window-floats)).

One editor-world system in `Phase::Game` (`EditorPanels::frame`) sets the dock area from `window_size_for`, hands tree keys to the Project or Inspector panel when its canvas is the topmost canvas under the pointer of the window the key went to (the editor window or the panel's float window) ([UI Inspector](UI%20Inspector.md#editor-panel)), refreshes the Inspector and the Profiler only while `layout.is_visible` says their tab is shown, and saves the layout when `DockSpace::revision` moved. `Game`, not `Bind`: `run_bind` of the editor world must see this frame's copy, and `run_dock_layout` (Bind) applies this frame's area. The Build panel is filled by the editor as a build runs, not refreshed.

`EditorPanels::show(key)` brings a panel to the front: `activate` its tab and `raise_float` its float; a float in an OS window is raised and focused (`dock_panel_os_window`, `IWindowControl::raise`, the window control `spawn` was given). A panel the layout lost is put back by `reconcile_dock_layout` first. A failed build calls `show("build")`.

### Layout file

`<user data>/dock_layout.toml` (`%APPDATA%/Wind/Editor/dock_layout.toml` on Windows): `dock_layout_to_text` of the layout. `DockLayoutFile` (`editor/src/dock_layout_file.cpp`) reads it and writes it to `dock_layout.toml.tmp` first, then renames it over the file, creating the directory; an empty path (no user data directory) reads and writes nothing.

| When | Does |
| --- | --- |
| Start | `load`. Missing, unreadable, not TOML, an unknown version, or a broken layout: the default (a warning when the file exists but is bad). A layout with none of the four panels: the default. Otherwise `reconcile_dock_layout` drops unknown keys and puts a missing panel beside the Inspector (Center), or in the first stack when the Inspector is missing too |
| Each frame | `save` when `DockSpace::revision` differs from the last one written: a tab activated, a float raised, a drag committed, a float window moved, resized, or closed (saved the frame after: the dock layout pass runs after `EditorPanels::frame`). Changes the editor makes itself (`show`) are not saved until the next one or quit |
| Start, floats | Each float of the loaded layout opens its window at its stored rect, relative to where the editor window is. A float whose top row is on no display is moved onto the editor window's display (and saved there) |
| Quit | `save` in `on_quit` |

The Project tab (`explorer.xml`, `ExplorerPanel`) is the open project's files as a tree, built with `<engine/ui/tree.h>` and the row recipe of [UI](../modules/UI.md#trees). `scan_project` (`editor/src/project_scan.cpp`) reads the project directory into `ProjectEntry` values when a project opens and on Refresh; nothing watches the disk between scans. It hides names that start with `.`, the build trees (`build`, `build-*`, `cmake-build-*`, `out`), and `.meta` sidecars, does not follow directory symlinks, and stops at 20000 entries (the line above the tree says so). Folders come first, then files, each by name ignoring case. A node's key is its path under the project (`assets/ui/menu.xml`), so Refresh keeps the folders that were expanded and the selection while they still exist. Folders start collapsed; opening a project starts over. The right column shows the selected path, then `File, 2.0 KB` or `Folder, 3 items`. A project that does not read leaves the tab empty.

The Inspector and Profiler hold a pointer to the game world only between attach and detach. Its rows are plain copies of the engine's snapshot rows. Details: [UI Inspector](UI%20Inspector.md), [UI Profiler](UI%20Profiler.md).

Both panels inspect the world bound to `kPrimaryWindow`. A game with a second world (a tool window) shows only its primary world.

## Build

Play first builds the project's game module. `ProjectBuild` (`editor/src/project_build.cpp`) runs `cmake` through `ProcessCall` ([Process](../modules/Process.md)), one step at a time, while frames keep running:

1. Configure, unless `<project>/build-editor/CMakeCache.txt` has `Wind_DIR` equal to `<sdk>/cmake` (`configured_for`): `cmake -S <project> -B <project>/build-editor -DCMAKE_PREFIX_PATH=<sdk> -DWind_DIR=<sdk>/cmake -DCMAKE_CONFIGURATION_TYPES=DebugGame;Release` (`Debug` against a Debug SDK). No generator is passed: CMake's default, or the one already in the cache. A cache made by the game's own preset for the same SDK is reused.
2. `cmake --build <project>/build-editor --config DebugGame --target <target> --parallel` (`Debug` against a Debug SDK). CMake reruns its configure itself when the game's `CMakeLists.txt` changed.
3. The module is the path `engine_add_game` recorded in `<build>/wind/<target>.<config>.module` ([CMake](../build/CMake.md#game-module-engine_editor-or-sdk-mode)). No record is an error that names the target.

Both steps run in the project directory with `VSLANG=1033` (MSBuild writes English) and `MSBUILDDISABLENODEREUSE=1` (no MSBuild node outlives the build; the call's job would end it anyway). Each step's command line (`> cmake ...`) and output go to the Build tab and the command to the log. A step that exits non-zero ends the build with "Configure failed" or "Build failed (exit code N)"; CMake missing from `PATH` is "CMake was not found". On failure the editor brings the Build panel to the front (`EditorPanels::show`), the status line says why, and the summary above the log is the first error line.

The Build tab (`build.xml`, `BuildPanel`): a summary line and the log, one 18px row per line (virtualized), the last 5000 lines. `tone_of` colors a line red for `: error `, `: fatal error `, or `CMake Error`, and amber for `: warning ` or `CMake Warning`. New lines scroll the log to the end: `logScroll` is set past it, and the bound scroll is clamped to the content ([UI Input](UI%20Input.md)).

Cancel (the Play button while building) ends the running step's process and everything it started; no outcome follows. Closing the editor during a build does the same.

## Play

After a successful build, `PlaySession::play` (`editor/src/play_session.cpp`) with the built module:

1. `load_game_module(module, live_root)`: copy the module and its `.pdb` into `live/<n>/`, load the copy, resolve the three exports, compare the build id ([Core](../modules/Core.md)). An error is shown in the status line and the editor stays idle.
2. `load_catalog(<module dir>/assets)`. An error unloads the module.
3. Remember the worlds and windows that exist now, and the frame pacing (`vsync`, `max_fps`): a game may change it for the whole process ([Windowing](Windowing.md#frame-pacing)).
4. `wind_create_game(services)`.
5. Apply the game's `primary_window()` to `kPrimaryWindow`: title, size, position when set, borderless, always on top. `transparent` cannot change after creation; the status line says so. `resizable` and `maximized` are not applied.
6. `EngineHost::attach_game` (icon, bind, UI, audio, size), then `on_start`.
7. `IPlayHost::attach_tools` with the world of `kPrimaryWindow`: `set_inspector_attached` and `set_ui_profiler_attached` on that world. Pick starts off, so the game gets its clicks.

## Stop

`PlaySession::stop`. Nothing built from game code may survive the unload at the end:

1. `IPlayHost::detach_tools`: detach the inspector and profiler from the game world and clear both panels. The game world still exists and is still bound.
2. `on_quit`.
3. `EngineHost::detach_game`: unbind `kPrimaryWindow`, clear its command buffer (its `CmdDrawUI` entries point into game documents), reset its NanoVG context and register `builtin::font_ui` again, clear drag region and click-through, overlay mode `Auto`, `paused` false.
4. Destroy every world that did not exist before Play. `Worlds::destroy` closes the dock float windows of a game world's `DockSpace` with it ([Docking](Docking.md#os-window-floats)).
5. Close every window that was not open before Play, except one bound to a world that was (a panel's float window opened while playing).
6. `InputSystem::reset`, then `IAudioSystem::stop_all`.
7. `unload_catalog(<module dir>/assets)`.
8. `wind_destroy_game`.
9. Destroy the `GameModule`: unload the copy and delete `live/<n>/`.
10. Put back the idle title and style of `kPrimaryWindow` (its size stays) and the frame pacing remembered at Play.

`kPrimaryWindow` then has no world. `draw_all` clears it to black and presents it.

## Quit

| What happened | Result |
| --- | --- |
| The game sets `ApplicationState::running = false` while playing (its own menu, a fatal report) | `on_frame_end` sets it back and stops the game. The editor keeps running |
| Close button of the editor window | Editor quits. A running game is stopped in `on_quit`, before the host disposes |
| Close button of `kPrimaryWindow` while playing | `WindowCloseRequestedEvent` in the game world, as in an exported build |
| Close button of `kPrimaryWindow` between plays | No world, the event is dropped |

An OS quit request (`SDL_EVENT_QUIT`) while playing looks the same as the game quitting, so it stops the game and the editor stays.

## Build layout

The editor's assets and catalog land in `bin/assets/editor/` (`ENGINE_RUNTIME_ASSETS_DIR`, see [CMake](../build/CMake.md)), in `build-editor/bin/<config>/` and in the SDK's `bin/`. A game module built against the SDK sits in the game's own `bin/<config>/` with only its `assets/` beside it; Play loads `<module dir>/assets/catalog.toml`, and the engine's assets come from the editor's `bin/assets/engine/`.

## Limits

- `engine.dll` is loaded once and stays locked while the editor runs. An engine change needs an editor restart and a game rebuilt against it. A module with another build id is refused with both ids in the status line.
- The game runs in the editor's process. A crash in game code, including code that keeps running after a fatal report, takes the editor down.
- `transparent`, `resizable`, and `maximized` of the game window are fixed at editor start.
- The game's window icon stays on `kPrimaryWindow` after Stop.
- The editor has no project list and no Open dialog: the [Launcher](Launcher.md) owns that and starts the editor with `--project`.
- Play always builds `DebugGame` (or `Debug` against a Debug SDK). No Release play yet.
- The game is built with CMake's default generator when the editor configures; a build directory configured with another generator keeps it.
- Long log lines are cut at the panel's right edge; the summary line shows the first error in full width.
- Against a Release SDK, game code gets no STL checks or CRT debug heap (`DebugGame` is `/MD`). That needs a Debug SDK and a Debug game.
- The panels show only the world of `kPrimaryWindow`.
- The Project tab only browses: no open, rename, create, delete, or drag. Files changed on disk show after Refresh.
- Panels cannot be closed or reset to the default layout from the UI; delete `dock_layout.toml` to start from the default.
- `taskkill` without `/F` posts `WM_CLOSE` to one top-level window of the process, which may be a float window: that float docks back and the editor keeps running.
- `wind-cli click` cannot switch dock tabs: tabs answer the mouse, not a command.
- `wind-cli launch` is Windows only, like `ProcessLauncher::launch`.

## Tests

`editor/tests/play_session_test.cpp` (`wind_editor_tests`) drives `PlaySession` with headless services (`tests/fixtures/fake_services.h`), a recording `IPlayHost`, and the fixture module: Play applies the window and attaches the game and then the tools to the game world, Stop runs in the order above (tools first, while the game world is still bound), puts back the frame pacing the fixture changed, and deletes the copy, Play again works, a wrong build id and a catalog error leave nothing behind, the destructor stops. `editor/tests/project_build_test.cpp` drives `ProjectBuild` with a scripted `IProcessLauncher`: configure then build of a fresh directory and the module record, a cache for this SDK skips configure, a cache for another SDK configures again, a Debug SDK builds Debug, a failed configure or build, CMake missing, no module record, and cancel. `build_panel_test.cpp` covers the tones, the first error, scrolling, and the line cap; `editor_options_test.cpp` the command line; `editor_cli_test.cpp` the `wind-cli` replies in each `RunState`, the refusals, and the toolbar request each command records. `editor/tests/editor_panels_test.cpp` covers the default layout, the dock area and panel canvases (and a window resize), visible-only refresh of the Inspector and Profiler, `show` (hidden, lost, and under another float), the layout saved on a revision change and read by the next start, `save_layout`, a corrupt file and a layout of unknown panels falling back to the default, reconcile of a saved layout, tree keys only to the Project panel under the pointer, and attach and detach, and with the fake window control (`EditorPanelsWindowsTest`): a floated panel's OS window bound to the editor world, a float window owned by the editor window with the utility style, `show` raising a floated panel's window (and not a docked one's), a saved float reopening its window at its rect and a native move saved, and tree keys in a float window; `play_session_test.cpp` also checks that Stop keeps a window the editor's world opened while playing; `dock_layout_file_test.cpp` covers the layout file round trip, a missing or corrupt file, and an empty path; `explorer_panel_test.cpp` covers what the scan hides and its order, the Project rows, expanding, selecting, tree keys, and Refresh keeping what still exists; `inspector_panel_test.cpp`, `profiler_panel_test.cpp`, and `profiler_chart_test.cpp` cover the panels. `wind_editor_tests` compiles every editor source except `main.cpp` and `editor_app.cpp`, with wind_editor's generated `asset_ids.h`. `tests/game_module_test.cpp` covers the loader.

## See also

- [Editor Plan](../architecture/Editor%20Plan.md)
- [Core](../modules/Core.md)
- [Windowing](Windowing.md)
- [CMake](../build/CMake.md)
