# Editor

`wind_editor` is the editor host. It is built only in the engine repo with `ENGINE_EDITOR` (preset `vs-editor`), from `editor/`, and shipped to games as the editor SDK. It is an engine client like a game: it includes only `<engine/...>` and links the shared `engine`. It opens a project, builds its game module with CMake against its own SDK, plays it in `kPrimaryWindow`, inspects the project's files and the game's UI, profiles its UI, and stops it. The plan is [Editor Plan](../architecture/Editor%20Plan.md).

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

1. `EngineHost::init` (an `EngineError` goes to `report_startup_error`). Without `--project` the editor reports "No project to open. Start the editor from the Wind launcher, or run wind_editor --project <dir>." through `IFatalError` (a message box) and exits with 1: a project opens only from the [Launcher](Launcher.md), the command line, or `wind-cli open`. Then `open_primary` with `WindowDesc{"Game", 800x600}`. That is `kPrimaryWindow`, the game's window.
2. `load_catalog(assets_root() / "editor")`: the editor's own `catalog.toml`. A failure is fatal.
3. `Worlds::add` for the editor world, `open_window({"Wind Editor", 1280x800})`, `bind_window`, `enable_ui`, and one `UiCanvas` (`FillWindow`) with `assets/ui/editor.xml` and `assets/css/editor.css` over `EditorViewModel`. `EditorPanels` spawns seven panel canvases (`Fixed`): `assets/ui/explorer.xml`, `assets/ui/hierarchy.xml`, `assets/ui/world_view.xml`, `assets/ui/ui_tree.xml`, `assets/ui/inspector.xml`, `assets/ui/profiler.xml`, and `assets/ui/build.xml`, all with `assets/css/panels.css`, and one `DockSpace` (order 1) that places them, with the saved layout ([Panels](#panels)). `OpeningOverlay` (`editor/src/opening_overlay.cpp`) spawns the startup build overlay canvas (`assets/ui/opening.xml`, order 100, `Fixed` with an empty rect while closed).
4. `purge_game_module_copies(<user data>/live)`. The user data directory is `user_data_directory("Wind", "Editor")`, read once at start; it also holds `dock_layout.toml` ([Layout file](#layout-file)). Without one the copies go to `<temp>/wind_editor/live` and the layout is not kept (logged).
5. `EditModule`, `PlaySession`, and `ProjectBuild` over `EngineServices::processes`. The SDK root is `<assets root>/../..`; `read_sdk_manifest` there gives the version and configuration. A missing or bad `sdk.toml` is logged and leaves the editor unable to build.
6. Opens the `--project` directory. When the project fits the SDK and `--play` was not passed, starts an opening module build under `OpeningOverlay` and loads the built module and catalog into `EditModule` ([Edit module and opening build](#edit-module-and-opening-build)). Only the panel layout is remembered between runs.

Opening a project reads `<dir>/wind_project.toml` (`read_wind_project`). A read error, a missing SDK, or an `engine` version other than the SDK's `version` shows in the status line and leaves Play and Export disabled: "The project needs engine 0.2.0; this editor is 0.1.7a." The versions are compared as exact text, stage included (`0.1.7` is not `0.1.7a`). The check is `check_project` (`editor/src/project_check.cpp`), shared by the GUI and the batch run, so both say the same thing.

Command line:

| Option | Effect |
| --- | --- |
| `--project <dir>` | Open this project (the directory with `wind_project.toml`). Required |
| `--play` | Press Play when the loop starts: build, then play. Needs `--project` |
| `--batch` | Run one command without a window and exit ([Export](#export)). Needs `--project` and a command |
| `--export [<dir>]` | The batch command: build the standalone executable and copy it to `<dir>`; `<project>/export/<target>` without one. A path is anything after `--export` that does not start with `--` |
| `--development` | With `--export`: the Development build (the game's `DebugGame` configuration, linked with the Development player: your code unoptimized with symbols, the UI profiler, the CLI server) instead of Release (the Shipping player). Needs a Release SDK |
| `--log-file <path>` | Also write the batch output lines to `<path>` (truncated first) |

`--batch` without a command, without `--project`, a command without `--batch`, or `--development` without `--export` is a usage error: the reason on standard error and exit code 2, before anything starts. Other unknown arguments are a warning (GUI) or ignored (batch).

## Window

A toolbar, and the panels' dock space below it ([Panels](#panels)).

| Control | Binding | Does |
| --- | --- | --- |
| Play / Cancel / Stop | `togglePlay`, `playLabel`, `isPlaying` (`checked`, red while building or playing) | Idle: build and play. Building: cancel the build. Playing: stop. Disabled with no playable project |
| Status line | `statusText` | Ready, Building, Playing, Stopped, the game quit, or why opening, building, or Play failed |
| Project line | `projectText` | The project's name and directory |

The commands are `engine::ui::MethodCommand` bound to `Toolbar` methods. Play/Stop only records an `EditorRequest` (`wind-cli` `play` and `stop` record the same one); `Toolbar::show_state` (`RunState` Idle, Building, Playing) sets what Play/Stop does. The editor window's `WindowCloseRequestedEvent` is read by one editor-world system that also only records. Every transition runs in `RunHooks::on_frame_end`, after the frame drew, because Play and Stop create and destroy worlds that no system of that frame may still be walking.

## wind-cli

`EditorApp::run` passes `RunHooks::cli` with `kind` `editor`, so the descriptor says this process is an editor, and a `handle` that calls `EditorCli` (`editor/src/editor_cli.cpp`) with `EditorFacts` read from `EditorApp` (`facts()`: the project name when one is open and playable, its directory, the SDK version). The commands and their replies are in [CLI](CLI.md#editor-commands).

`EditorCli` reads the toolbar (`Toolbar::state`, `playable`, `status`) and acts only through it, so a command and a button do the same thing:

- `play` calls `Toolbar::toggle_play` when Idle and playable: `EditorRequest::Play`.
- `stop` calls it when Building or Playing: `EditorRequest::Stop`, which cancels the build or stops the game.
- `open` keeps the directory; `on_frame_end` takes it (`take_open`) and calls `open_project` before it reads the toolbar request, so a Play in the same frame builds the project just opened.

`handle` runs inside `cli::drain`, after the frame drew and before `on_frame_end`, which then makes the transition in the same frame. A UI command (`tree`, `click`, ...) reaches the game world while playing; between plays `kPrimaryWindow` has no world and the answer is `no world on window 0` at once. `--window` with the editor window's id reaches the editor's own canvases.

## Panels

`EditorPanels` (`editor/src/editor_panels.cpp`) owns the `EditorSelection` ([Inspector](#inspector)), `ExplorerPanel`, `HierarchyPanel`, `WorldViewPanel`, `UiTreePanel`, `InspectorPanel`, `ProfilerPanel`, and `BuildPanel`, their canvases, and one engine `DockSpace` ([Docking](Docking.md#host)) on the editor window. The dock space fills the window below the 56px toolbar (`EditorPanels::kToolbarHeight`, `.toolbar` of `editor.css`), from `order` 1 (`kDockOrder`, above the editor's own canvas). The engine's dock systems draw the tab strips, splitters, floats, and drop preview, handle the pointer on them, and write each panel canvas's rect, order, and window; an inactive tab is `Fixed` with an empty rect, so it takes no clicks. Tabs are as wide as their titles ([Tab width](Docking.md#tab-width)).

| Key | Title | Canvas |
| --- | --- | --- |
| `project` | Project | `explorer.xml` |
| `hierarchy` | Hierarchy | `hierarchy.xml` |
| `world` | World | `world_view.xml` |
| `ui_tree` | UI Tree | `ui_tree.xml` |
| `inspector` | Inspector | `inspector.xml` |
| `profiler` | Profiler | `profiler.xml` |
| `build` | Build | `build.xml` |

`EditorPanels::default_layout()`: Project and Hierarchy tabbed on the left (Project shown, 20% of the width, full height); in the middle, World, UI Tree, and Profiler as tabs (UI Tree shown) over Build (Build 30% of that column); the Inspector on the right (40% of what Project leaves), full height, so it stays in view beside the tree that selects. No panel is closable yet: there is nothing to reopen one with. Tabs can be reordered, split, tabbed together, and floated. The dock space is `DockFloatMode::OsWindow`: a panel dropped outside the dock area or Shift-dragged opens in an OS window of its own, bound to the editor's world, titled with its active tab, moved and resized by the OS, a tool window of the editor window (above it, minimized with it, no taskbar entry); tabs drag between the editor window and those windows, and a float window's close button docks its panels back ([OS window floats](Docking.md#os-window-floats)).

One editor-world system in `Phase::Game` (`EditorPanels::frame`) sets the dock area from `window_size_for`, hands tree keys to the Project, Hierarchy, or UI Tree panel when its canvas is the topmost canvas under the pointer of the window the key went to (the editor window or the panel's float window) ([UI Inspector](UI%20Inspector.md#editor-panels)), opens a newly selected `.world` file in the World panel and activates its tab (`show("world")`), syncs the World panel's preview world to the Hierarchy and Inspector panels, updates the World panel's editor camera (`WorldViewPanel::frame`), refreshes the Hierarchy, the UI Tree, the Inspector, and the Profiler only while `layout.is_visible` says their tab is shown (the UI Tree's `sync_selection` runs when it is hidden), and saves the layout when `DockSpace::revision` moved. `Game`, not `Bind`: `run_bind` of the editor world must see this frame's copy, and `run_dock_layout` (Bind) applies this frame's area. A second editor-world system in `Phase::Render` (`EditorPanels::render`) renders the World panel's preview world into its viewport rect when the World tab is visible (`render_world_viewport`). The Build panel is filled by the editor as a build runs, not refreshed.

`EditorPanels::show(key)` brings a panel to the front: `activate` its tab and `raise_float` its float; a float in an OS window is raised and focused (`dock_panel_os_window`, `IWindowControl::raise`, the window control `spawn` was given). A panel the layout lost is put back by `reconcile_dock_layout` first. A failed build calls `show("build")`.

### Hierarchy panel

The Hierarchy tab (`hierarchy.xml`, `HierarchyPanel` in `editor/src/hierarchy_panel.cpp`, `HierarchyViewModel` in `editor/src/hierarchy_view_model.cpp`, `HierarchyRowViewModel` in `editor/src/hierarchy_row_view_model.cpp`) shows the entity tree of the active ECS world (`game_` during Play, or `WorldViewPanel::edit_world()` in Edit Mode when a `.world` file is open):

- When no world is active, `statusText` is `"Open a .world file in Project, or press Play."` and `rows` is empty.
- When an active world is present, `statusText` shows `"Live game  (<N> entities)"` during Play or `"<filename>  (<N> entities)"` in Edit Mode.
- Root entities are alive entities without a valid `Parent`; child entities follow `Children` (and `Parent::entity`), flattened through `engine::ui::flatten_tree` and `engine::ui::TreeExpansion<std::uint32_t>` (expanded by default). Row view-models are reused by `entity.index` across refreshes.
- Each row shows `EntityName::id` (or `entity_<index>` when unnamed) plus `"  [ActiveCamera]"` for the world's `ActiveCamera` entity or `"  [Camera]"` for any other entity with a `Camera` component.
- Clicking a row (`select`) or navigating with tree keys (`Up`, `Down`, `Left`, `Right`, `Home`, `End` via `HierarchyPanel::navigate`) selects the entity and publishes `EntitySelection` to `EditorSelection` so the Inspector displays its components. Switching worlds, detaching on Stop, or destroying the selected entity clears `selected_` and clears `EditorSelection` when it holds an `EntitySelection`.

### World panel and editor camera

The World tab (`world_view.xml`, `WorldViewPanel` in `editor/src/world_view_panel.cpp`, `WorldViewViewModel` in `editor/src/world_view_view_model.cpp`) previews a `.world` scene in Edit Mode:

- Selecting a `.world` file in the Project tab (`WorldViewPanel::sync_selection`) opens it into an isolated edit `ecs::World` (`edit_world_`), registers the open project's `EditModule` component serializers on that world's `WorldSerializer` (`set_component_registrar`), calls `load_world_from_toml` with `UnknownComponentMode::Preserve` (so a `.world` still opens when the startup build was skipped or failed), and brings the `World` tab to the front (`EditorPanels::show(kWorld)`). Selecting a non-world file leaves the current preview world open.
- The 36 px header bar (`world-header`, `WorldViewPanel::kHeaderHeight`) has a `2D`/`3D` projection toggle button (`toggleProjection`, `projectionLabel`, `MethodCommand`), a `Reset` button (`resetCamera`, `MethodCommand`), and `statusText` (`"<key>  (<N> entities)"` or `"No world open."`). The viewport area below the header (`world-viewport`) binds `viewportBg`: `#1e1f22` with `emptyText` centered while no world is loaded (or on a read/parse error), and `#00000000` (transparent) once a world is loaded so the scene rendered into that scissored sub-rectangle in `Phase::Render` (`engine::render_world_viewport`, before `Phase::UiRender` draws the panel chrome) shows through.
- Initial camera setup (`init_camera_from_scene`): if the loaded world has a valid `ActiveCamera` entity with `Camera` and `Transform`, the editor camera starts from its `Camera`, world position, and world rotation (`3D` when `Projection::Perspective`, otherwise `2D` `Projection::Orthographic`); otherwise it starts as a default 2D orthographic camera (`ortho_size = 10`, `position = {0, 0, 10}`). Clicking `2D`/`3D` toggles projection; `Reset` restores the initial pose and zoom.
- Editor camera navigation (`WorldViewPanel::handle_input`, called from `WorldViewPanel::frame`, reading `MouseEvent` and `KeyEvent` through its own `EventCursor`s on the panel canvas's window while the pointer is inside the viewport below the header and the World canvas is the topmost canvas there):
  - **2D (`Projection::Orthographic`)**: mouse drag pans in world units (`2 * ortho_size / viewport.h` per pixel); mouse wheel zooms `ortho_size` (clamped to `[0.1, 10000]`).
  - **3D (`Projection::Perspective`)**: right-mouse drag orbits/rotates yaw and pitch (`euler_xyz_`); left/middle-mouse drag pans along the camera's right and up axes; mouse wheel dollies along the camera's forward axis; holding `W`/`A`/`S`/`D`/`Q`/`E` while right-dragging or while the pointer is in the viewport flies the camera (`kFlySpeed = 10`).
- `EditModule` lifecycle safety: before `EditModule::load` or `EditModule::unload` runs (and on project switch or quit), `EditorApp` calls `WorldViewPanel::before_module_unload()` (or `close()`), which resets `edit_world_` while the game DLL is still mapped in memory so game component pool destructors never execute unmapped code; after `EditModule` reloads, `WorldViewPanel::reload()` re-opens `open_path_`.

### Layout file

`<user data>/dock_layout.toml` (`%APPDATA%/Wind/Editor/dock_layout.toml` on Windows): `dock_layout_to_text` of the layout. `DockLayoutFile` (`editor/src/dock_layout_file.cpp`) reads it and writes it to `dock_layout.toml.tmp` first, then renames it over the file, creating the directory; an empty path (no user data directory) reads and writes nothing.

| When | Does |
| --- | --- |
| Start | `load`. Missing, unreadable, not TOML, an unknown version, or a broken layout: the default (a warning when the file exists but is bad). A layout with none of the seven panels: the default. Otherwise `reconcile_dock_layout` drops unknown keys and puts a missing panel beside the Profiler (Center), or in the first stack when the Profiler is missing too. A layout saved before the Hierarchy, World, or UI Tree panel existed gets it tabbed with the Profiler |
| Each frame | `save` when `DockSpace::revision` differs from the last one written: a tab activated, a float raised, a drag committed, a float window moved, resized, or closed (saved the frame after: the dock layout pass runs after `EditorPanels::frame`). Changes the editor makes itself (`show`) are not saved until the next one or quit |
| Start, floats | Each float of the loaded layout opens its window at its stored rect, relative to where the editor window is. A float whose top row is on no display is moved onto the editor window's display (and saved there) |
| Quit | `save` in `on_quit` |

The Project tab (`explorer.xml`, `ExplorerPanel`) is the open project's files as a tree, built with `<engine/ui/tree.h>` and the row recipe of [UI](../modules/UI.md#trees). `scan_project` (`editor/src/project_scan.cpp`) reads the project directory into `ProjectEntry` values when a project opens and on Refresh; nothing watches the disk between scans. It hides names that start with `.`, the build trees (`build`, `build-*`, `cmake-build-*`, `out`), and `.meta` sidecars, does not follow directory symlinks, and stops at 20000 entries (the line above the tree says so). Folders come first, then files, each by name ignoring case. A node's key is its path under the project (`assets/ui/menu.xml`), so Refresh keeps the folders that were expanded and the selection while they still exist. Folders start collapsed; opening a project starts over. Selecting a row (a click or a tree key) makes it the editor's selection, which the Inspector shows. A project that does not read leaves the tab empty.

The Hierarchy, UI Tree, Inspector, and Profiler hold a pointer to the game world only between attach and detach. Their rows are plain copies of the engine's snapshot rows. Details: [UI Inspector](UI%20Inspector.md), [UI Profiler](UI%20Profiler.md).

They inspect the world bound to `kPrimaryWindow`. A game with a second world (a tool window) shows only its primary world.

### Inspector

The Inspector tab (`inspector.xml`, `InspectorPanel`) shows the editor's selection: a title, a subtitle, and sections with a heading and lines. A section's chevron or heading collapses it; the collapsed headings are kept across selections ("Content" stays collapsed from file to file).

`EditorSelection` (`editor/src/editor_selection.h`) is one `SelectionTarget`: nothing, an `AssetSelection` (a project file or folder: its key, path, and what the scan saw), a `UiElementSelection` (a window of the game; the element is the probe's selection of that window), or an `EntitySelection` (an `ecs::Entity` in the active world). The newest selection wins; selecting in one panel leaves the other panel's own highlight. `revision` moves on every select, even of the same thing.

| Who | Writes |
| --- | --- |
| Project tab | A row click or tree key: that entry. Refresh: the entry again when it is the editor's selection and still exists (read again), cleared when it is gone. Close or open another project: cleared when it is a file or folder |
| Hierarchy tab | A row click or tree key: that entity (`EntitySelection`). Switching worlds, Stop, or destroying the entity: cleared when it is an entity |
| UI Tree tab | A new probe selection (a row, a tree key, a pick click in the game): that element. Stop: cleared when it is a UI element |

A file or folder is read when it is selected (the revision moved), not every frame: `inspect_asset` (`editor/src/asset_inspection.cpp`).

| Section | Shows |
| --- | --- |
| Folder | `Path:` and `Items:` (what the scan shows) |
| File | `Path:` and `Size:`; a file that does not open says so |
| Import | When `<file>.meta` exists: `GUID:` and `Importer:` from `parse_asset_meta`; for `texture` and `ui_image` the color space, filter, wrap, layout, pixels per unit, and sprite count; for `audio` the bank, volume, pitch, and loop; for `strings` whether it is the key source; for `world` the entity count and active camera. A sidecar that does not parse says why |
| Content | A PNG: `PNG image, W x H` from its IHDR. Otherwise the first 64 KB, when they hold no NUL byte, as text: CR dropped, tabs as 4 spaces, at most 500 lines, and `... the first N lines of SIZE` when cut. An empty file is `Empty`. A binary file has no Content |

An entity (`EntitySelection`) is inspected on every refresh while the tab is shown via `inspect_entity` (`editor/src/entity_inspection.cpp`) against the active world (`game_` during Play, or `edit_world_` in Edit Mode):
- `Entity` section: `id` (`EntityName::id` or `(unnamed)`), `handle` (`<index>:<generation>`), `parent` (when valid), `children` count (when non-empty), and `active_camera: true` (when the entity is the world's `ActiveCamera`).
- One section per registered component on the entity via `WorldSerializer::entries()` (`transform`, `camera`, `sprite`, `WIND_COMPONENT` game components, etc.), with each serialized `FieldTable` entry formatted as `<key> = <value>`.
- One `<name> (unregistered)` section per preserved unknown component in `UnknownComponents`, formatted from its preserved `FieldTable`.

A UI element is copied on every refresh while the tab is shown: [UI Inspector](UI%20Inspector.md#editor-panels).

## Edit module and opening build

Opening a project that fits the SDK (unless `--play` is set) starts a `BuildKind::Module` build at once and opens `OpeningOverlay` (`editor/src/opening_overlay.cpp`, `assets/ui/opening.xml`, order 100): a full-window backdrop (`FillWindow` while open; `Fixed` with an empty rect while closed so it takes no clicks) with a centered card showing `"Opening <project>..."`, the current CMake step (`"Configuring CMake..."`, `"Building <target>..."`), a progress bar (`progressWidth` bound via `var-w` to `.opening-progress-fill`, driven from `> cmake -S`, `> cmake --build`, and `[N/M]` or `N%` build lines), the latest output line, and a `Skip` button (`MethodCommand`). While the opening build runs, the toolbar stays `RunState::Idle`:

- If the user presses `Skip`, the build is cancelled, the overlay closes, the Build summary shows `"Startup build skipped."`, and the project stays open so `.world` files can still load with `UnknownComponentMode::Preserve` (`FieldTable`).
- If `wind-cli play` arrives while the opening build is running, `EditorApp::play` promotes the running opening build into a Play build (closing the overlay and switching the toolbar to `RunState::Building`) without restarting CMake.
- If the opening build fails, the overlay closes, the Build panel comes to the front (`EditorPanels::show`), and the project stays open and playable so `.world` files can still be inspected through `UnknownComponentMode::Preserve` and the user can fix the build and press Play.
- When the opening build succeeds, `EditModule::load` (`editor/src/edit_module.cpp`) loads a live copy of the built game module and `<module dir>/assets/catalog.toml`, `EditModule::register_components` registers the module's `WIND_COMPONENT` serializers (`wind_register_game_components`) on `world_->ctx<WorldSerializer>()`, and `WorldViewPanel::reload()` re-opens any `.world.toml` preview so game components deserialize with the freshly loaded module.

Before `PlaySession::play` runs, `WorldViewPanel::before_module_unload()` drops any preview `ecs::World`, and `EditModule::unload` resets every registered `WorldSerializer` to the built-in engine components (before `FreeLibrary` unmaps the module's `std::function` targets), unloads the catalog, and deletes the edit live copy so `PlaySession` owns the module and catalog during Play. After `PlaySession::stop` (unless the editor is quitting), `EditModule` reloads the last built module, re-registers its component serializers for Edit Mode, and `WorldViewPanel::reload()` restores the preview world.

## Build

Opening a project or pressing Play builds the project's game module. `ProjectBuild` (`editor/src/project_build.cpp`) runs `cmake` through `ProcessCall` ([Process](../modules/Process.md)), one step at a time, while frames keep running:

1. Configure, unless `<project>/build-editor/CMakeCache.txt` has `Wind_DIR` equal to `<sdk>/cmake` (`configured_for`): `cmake -S <project> -B <project>/build-editor -DCMAKE_PREFIX_PATH=<sdk> -DWind_DIR=<sdk>/cmake -DCMAKE_CONFIGURATION_TYPES=DebugGame;Release` (`Debug` against a Debug SDK). No generator is passed: CMake's default, or the one already in the cache. A cache made by the game's own preset for the same SDK is reused.
2. `cmake --build <project>/build-editor --config DebugGame --target <target> --parallel` (`Debug` against a Debug SDK). CMake reruns its configure itself when the game's `CMakeLists.txt` changed.
3. The module is the path `engine_add_game` recorded in `<build>/wind/<target>.<config>.module` ([CMake](../build/CMake.md#game-module-engine_editor-or-sdk-mode)). No record is an error that names the target.

Both steps run in the project directory with `VSLANG=1033` (MSBuild writes English) and `MSBUILDDISABLENODEREUSE=1` (no MSBuild node outlives the build; the call's job would end it anyway). Each step's command line (`> cmake ...`) and output go to the Build tab and the command to the log. A step that exits non-zero ends the build with "Configure failed" or "Build failed (exit code N)"; CMake missing from `PATH` is "CMake was not found". On failure the editor brings the Build panel to the front (`EditorPanels::show`), the status line says why, and the summary above the log is the first error line.

The Build tab (`build.xml`, `BuildPanel`): a summary line and the log, one 18px row per line (virtualized), the last 5000 lines. `tone_of` colors a line red for `: error `, `: fatal error `, or `CMake Error`, and amber for `: warning ` or `CMake Warning`. New lines scroll the log to the end: `logScroll` is set past it, and the bound scroll is clamped to the content ([UI Input](UI%20Input.md)).

Cancel (the Play button while building) ends the running step's process and everything it started; no outcome follows. Closing the editor during a build does the same.

## Play

After a successful build, `EditModule::unload` releases any Edit Mode module and catalog, and `PlaySession::play` (`editor/src/play_session.cpp`) runs with the built module:

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
3. `EngineHost::detach_game`: unbind `kPrimaryWindow`, clear its command buffer (its `CmdDrawUI` entries point into game documents), drop the UI images (`reset_ui_cache`; fonts stay registered) and register `builtin::font_ui` again, clear drag region and click-through, overlay mode `Auto`, `paused` false.
4. Destroy every world that did not exist before Play. `Worlds::destroy` closes the dock float windows of a game world's `DockSpace` with it ([Docking](Docking.md#os-window-floats)).
5. Close every window that was not open before Play, except one bound to a world that was (a panel's float window opened while playing).
6. `InputSystem::reset`, then `IAudioSystem::stop_all`.
7. `unload_catalog(<module dir>/assets)`.
8. `wind_destroy_game`.
9. Destroy the `GameModule`: unload the copy and delete `live/<n>/`.
10. Put back the idle title and style of `kPrimaryWindow` (its size stays) and the frame pacing remembered at Play.

`kPrimaryWindow` then has no world (`draw_all` clears it to black and presents it), and `EditorApp::stop` reloads `EditModule` from the last built module so Edit Mode has the game's components and catalog again.

## Quit

| What happened | Result |
| --- | --- |
| The game sets `ApplicationState::running = false` while playing (its own menu) | `on_frame_end` sets it back and stops the game. The editor keeps running |
| Close button of the editor window | Editor quits. A running game is stopped in `on_quit`, before the host disposes |
| Close button of `kPrimaryWindow` while playing | `WindowCloseRequestedEvent` in the game world, as in an exported build |
| Close button of `kPrimaryWindow` between plays | No world, the event is dropped |

An OS quit request (`SDL_EVENT_QUIT`) while playing looks the same as the game quitting, so it stops the game and the editor stays.

## Export

Export builds the project as a standalone executable (the static engine, no editor, no module) and copies it out. Windows, Linux, and macOS only; Web and Android are not exported from the editor.

The engine is not compiled for an export. The SDK ships it built, the player libraries `<sdk>/player/Development` and `<sdk>/player/Shipping` ([CMake](../build/CMake.md#player-libraries)), so an Export compiles the game's code and links: seconds. An SDK without them (a Debug SDK, or one built with `ENGINE_SDK_PLAYER=OFF`, or macOS) falls back to building the engine from `<sdk>/source`, minutes, and only as Release; `WindConfig.cmake` says which it took in a status line of the configure, in the Build tab.

| | Development Build | Release |
| --- | --- | --- |
| Game configuration | `DebugGame` | `Release` |
| Linked with | Development player | Shipping player |
| Your code | unoptimized, symbols, asserts on | optimized, no symbols |
| Engine | optimized, symbols, UI profiler, CLI server | optimized, no profiler, no CLI server |
| `wind-cli` finds the game | yes | no |
| For | testing an exported game, profiling it, driving it from a script | players |

From the GUI: the Export button next to Play (`EditorRequest::Export`; enabled when the project fits the SDK and nothing runs, so not while a build or the game runs) opens the Export dialog, a `Popup` under the button (anchored to a `Stack` around it, so a press on the button while the dialog is open dismisses it, as a press anywhere outside does). `ExportDialog` (`editor/src/export_dialog.cpp`, owned by `Toolbar`; the XML binds the `export*` fields of `EditorViewModel`) has:

- the folder: a `TextInput` that starts as `<project>/export/<target>`, or as the last folder of this session for this project, and Browse... (`IWindowControl::request_open_folder`, [Windowing](Windowing.md); the dialog opens in the nearest existing directory of the text and the answer replaces the text). A relative folder is taken from the project;
- the Development Build checkbox, with a line saying what it is;
- a line under them that follows the text and the checkbox live: why the folder cannot be used (`export_directory_problem`: empty, a root, the project or a directory that holds it, a file, a non-empty directory no Export made; or a Development build against a Debug SDK, `export_flavour_problem`), in red, or in amber that an earlier export is there and Export replaces it;
- Cancel, and Export, which is disabled while the line is a problem, while the editor builds or plays, and while the project does not fit. Enter in the folder field presses it. Escape or a click outside closes the dialog; the folder and the checkbox stay for the next time.

Export closes the dialog and `EditorApp::start_export` starts the build. While it builds, the Play button is Cancel, as for Play (`RunState::Building`), and the log goes to the Build tab. After a successful build `finish_export` copies the result to the chosen folder and the status line says "Exported to <dir>". A failure is shown as for Play: the status line, the Build tab in front.

The build is a `ProjectBuild` of `BuildKind::Export`:

1. Configure in `<project>/build-export` (never `build-editor`: the two caches differ in what `Wind_DIR` leads to), unless its cache has `Wind_DIR` equal to `<sdk>/cmake`, `CMAKE_CONFIGURATION_TYPES` equal to the wanted list, and `WIND_EXPORT` ON (`configured_for`): `cmake -S <project> -B <project>/build-export [-G "Ninja Multi-Config"] -DWIND_EXPORT=ON -DCMAKE_PREFIX_PATH=<sdk> -DWind_DIR=<sdk>/cmake -DCMAKE_CONFIGURATION_TYPES=DebugGame;Release` (`Release` alone against a Debug SDK, `game_configurations`). `WindConfig.cmake` sees `WIND_EXPORT` and imports the SDK's player libraries as `engine`, or builds the engine from `<sdk>/source` when the SDK has none ([CMake](../build/CMake.md#standalone-executable-wind_export)); the game's own `CMakeLists.txt` runs unchanged either way. One build directory serves both flavours, so a Development export after a Release one only compiles the game again.
2. `cmake --build <project>/build-export --config <config> --target <target> --parallel`, `<config>` being `DebugGame` for a Development build and `Release` otherwise (`game_config`; always `Release` against a Debug SDK).
3. The result is the directory `engine_add_game` recorded in `<build>/wind/<target>.<config>.export`: the executable and its `assets/`. It sits beside nothing else: the engine is linked statically, so no `.dll` has to come along.
4. `copy_export` (`editor/src/project_export.cpp`) empties the target directory, creates it, writes `.wind_export` (`kExportMarker`) into it, and copies the result recursively, leaving out `*.pdb`, `*.ilk`, `*.exp`, `*.lib`, `*.idb`, `*.obj`, `*.o`, `*.a`, `*.tlog`, and `CMakeFiles`. Because Export empties its target, it only does that to a directory that does not exist, is empty, or has `.wind_export` from an earlier Export; any other directory is someone's files and is refused with a message that says so, and nothing in it is deleted. `export_directory_problem` also refuses a target that is a file system root, a file, the project, or a directory that holds the project, and `copy_export` one that overlaps the build output.

Batch: `wind_editor --batch --project <dir> --export [<dir>] [--development] [--log-file <path>]` runs the same steps without `EngineHost`, a window, or an audio device. `main` creates a `ProcessLauncher` and polls it every 10 ms until `BatchRun` (`editor/src/batch_run.cpp`) returns an exit code. `BatchRun` takes the SDK root from `main` (the parent of `engine::executable_directory()`, so `<sdk>/bin/wind_editor`), reads `sdk.toml` and the project, checks them with `check_project`, refuses `--development` against a Debug SDK, and then builds and copies. Output lines (its own progress, each `> cmake ...` command, cmake's output, `Exported to <dir>`, or `error: <reason>`) go to standard output and to `--log-file`; a failure also goes to the engine log.

| Exit code | Meaning |
| --- | --- |
| 0 | Exported |
| 1 | The SDK, the project, the build, or the copy failed (also an unwritable `--log-file`, and `--development` against a Debug SDK) |
| 2 | Usage error |

The Release `wind_editor` of an SDK is a GUI-subsystem executable on Windows (`/SUBSYSTEM:WINDOWS`; only Debug has a console), and the editor does not attach to the console of its parent: a shell does not wait for it and sees no output. A script waits and reads the code with `Start-Process -Wait -PassThru -FilePath <sdk>\bin\wind_editor.exe -ArgumentList '--batch','--project',$dir,'--export',$out,'--log-file',$log` and then `.ExitCode`, and reads `$log` for the output. CI does the same.

## Build layout

The editor's assets and catalog land in `bin/assets/editor/` (`ENGINE_RUNTIME_ASSETS_DIR`, see [CMake](../build/CMake.md)), in `build-editor/bin/<config>/` and in the SDK's `bin/`. A game module built against the SDK sits in the game's own `bin/<config>/` with only its `assets/` beside it; Play loads `<module dir>/assets/catalog.toml`, and the engine's assets come from the editor's `bin/assets/engine/`.

## Limits

- `engine.dll` is loaded once and stays locked while the editor runs. An engine change needs an editor restart and a game rebuilt against it. A module with another build id is refused with both ids in the status line.
- The game runs in the editor's process. A crash in game code, takes the editor down.
- `transparent`, `resizable`, and `maximized` of the game window are fixed at editor start.
- The game's window icon stays on `kPrimaryWindow` after Stop.
- The editor has no project list and no Open dialog: the [Launcher](Launcher.md) owns that and starts the editor with `--project`.
- Play always builds `DebugGame` (or `Debug` against a Debug SDK). No Release play yet.
- Export is desktop only (Windows, Linux, macOS; no Web, no Android, no `.app` bundle, signing, or installer). Against a Release SDK with player libraries it links the engine statically without compiling it; against one without (Debug SDK, macOS) it needs the SDK's `source/`, builds the engine and SDL from scratch on the first export, and is Release only. The export directory is emptied first.
- One operation at a time: Play, Export, and a batch run each own the build; no queue.
- The game is built with CMake's default generator on Windows (Visual Studio) and `Ninja Multi-Config` on Linux and macOS when the editor configures; a build directory configured with another generator keeps it.
- Long log lines are cut at the panel's right edge; the summary line shows the first error in full width.
- Against a Release SDK, game code gets no STL checks or CRT debug heap (`DebugGame` is `/MD`). That needs a Debug SDK and a Debug game.
- The panels show only the world of `kPrimaryWindow`.
- The Project tab only browses: no open, rename, create, delete, or drag. Files changed on disk show after Refresh.
- The Inspector only reads: nothing in it is editable yet. A file shows the first 500 lines of text; an image shows its size, not a preview.
- Panels cannot be closed or reset to the default layout from the UI; delete `dock_layout.toml` to start from the default.
- `taskkill` without `/F` posts `WM_CLOSE` to one top-level window of the process, which may be a float window: that float docks back and the editor keeps running.
- `wind-cli click` cannot switch dock tabs: tabs answer the mouse, not a command.
- The editor needs an audio device (SDL opens one at start). With none, set `SDL_AUDIODRIVER=dummy`.

## Tests

`editor/tests/play_session_test.cpp` (`wind_editor_tests`) drives `PlaySession` with headless services (`tests/fixtures/fake_services.h`), a recording `IPlayHost`, and the fixture module: Play applies the window and attaches the game and then the tools to the game world, Stop runs in the order above (tools first, while the game world is still bound), puts back the frame pacing the fixture changed, and deletes the copy, Play again works, a wrong build id and a catalog error leave nothing behind, the destructor stops. `editor/tests/edit_module_test.cpp` covers `EditModule`: loading a live copy and its catalog, registering `WIND_COMPONENT` serializers on a `World`, unloading (resetting registered serializers before freeing the module and deleting the live copy) while preserving `.world` loading through `UnknownComponentMode::Preserve`, `reset`, and missing module, wrong build id, and catalog errors. `editor/tests/opening_overlay_test.cpp` covers `OpeningOverlay`: spawning closed (`UiFit::Fixed` with an empty rect), opening (`UiFit::FillWindow`) and closing, parsing progress and step text from CMake and Ninja/Makefile output lines, and the `Skip` command. `editor/tests/world_view_panel_test.cpp` covers `WorldViewPanel`: opening a `.world` selected in `EditorSelection`, reporting parse errors, initializing and toggling 2D/3D editor camera modes and `Reset`, 2D drag-pan and scroll-zoom, 3D right-drag look and `WASD`/`QE` fly movement, emitting `CmdSetView` with the viewport rect and clear color in `Phase::Render`, and `before_module_unload()` / `reload()` across `EditModule` unload and reload. `editor/tests/hierarchy_panel_test.cpp` covers `HierarchyPanel`: the idle hint, displaying the entity hierarchy tree (`Parent`/`Children`, `EntityName`, unnamed fallback, `[ActiveCamera]` and `[Camera]` badges) of an Edit Mode `.world` and of an attached Play Mode game world, expanding and collapsing subtrees (`toggle`), selecting an entity (`select`) and publishing `EntitySelection`, keyboard tree navigation (`navigate`), and clearing invalid selections on world switch, entity destroy, or detach. `editor/tests/project_build_test.cpp` drives `ProjectBuild` with a scripted `IProcessLauncher`: configure then build of a fresh directory and the module record, a cache for this SDK skips configure, a cache for another SDK configures again, a Debug SDK builds Debug, a failed configure or build, CMake missing, no module record, and cancel; an export configures `build-export` with `-DWIND_EXPORT=ON` and `DebugGame;Release`, finds the `.export` record of Release or (Development) DebugGame, and configures again for a cache without `WIND_EXPORT`, for another SDK, or with other configurations; a Debug SDK exports Release whatever was asked. `project_export_test.cpp` covers `copy_export` (the file filter, replacing the target, overlaps, errors) and the refused targets, `batch_run_test.cpp` the exit codes of `BatchRun` with a scripted launcher (exported 0, `--development` exporting the DebugGame directory, failed configure or build 1, another engine version 1, a Development build against a Debug SDK 1, a target that holds the project 1), `toolbar_test.cpp` the Export button and its request and that the dialog's Export follows the toolbar's state, `export_dialog_test.cpp` the Export dialog over a fake window control (the default and the remembered folder, the live problem and warning lines, confirm and cancel, Browse with a scripted folder answer, a Debug SDK refusing the Development build). `build_panel_test.cpp` covers the tones, the first error, scrolling, and the line cap; `editor_options_test.cpp` the command line (`--batch`, `--export` with and without a directory, `--development`, `--log-file`, the usage errors); `editor_cli_test.cpp` the `wind-cli` replies in each `RunState`, the refusals, and the toolbar request each command records. `editor/tests/editor_panels_test.cpp` covers the default layout, the dock area and panel canvases (and a window resize), selecting a `.world` file opening the World panel and activating its tab, visible-only refresh of the UI Tree and Profiler, the Inspector following the newest selection of the Project tab, Hierarchy tab, or a pick (UI Tree hidden) and emptying on Stop, `show` (hidden, lost, and under another float), the layout saved on a revision change and read by the next start, `save_layout`, a corrupt file and a layout of unknown panels falling back to the default, reconcile of a saved layout, tree keys to the Project or Hierarchy panel under the pointer, and attach and detach, and with the fake window control (`EditorPanelsWindowsTest`): a floated panel's OS window bound to the editor world, a float window owned by the editor window with the utility style, `show` raising a floated panel's window (and not a docked one's), a saved float reopening its window at its rect and a native move saved, and tree keys in a float window; `play_session_test.cpp` also checks that Stop keeps a window the editor's world opened while playing; `dock_layout_file_test.cpp` covers the layout file round trip, a missing or corrupt file, and an empty path; `explorer_panel_test.cpp` covers what the scan hides and its order, the Project rows, expanding, selecting and the editor selection it writes, tree keys, Refresh keeping what still exists and leaving another panel's selection; `asset_inspection_test.cpp` covers the Inspector sections of a folder, a text file and its meta, texture and audio settings, a broken meta, a PNG, a cut text, and an empty or gone file; `inspector_panel_test.cpp` covers nothing selected, a file read only when selected, collapsed sections, a UI element, an entity (`Entity` metadata, serialized components, and preserved `UnknownComponents`), and detach; `ui_tree_panel_test.cpp`, `profiler_panel_test.cpp`, and `profiler_chart_test.cpp` cover the other panels. `wind_editor_tests` compiles every editor source except `main.cpp` and `editor_app.cpp`, with wind_editor's generated `asset_ids.h`. `tests/game_module_test.cpp` covers the loader.

## See also

- [Editor Plan](../architecture/Editor%20Plan.md)
- [Core](../modules/Core.md)
- [Windowing](Windowing.md)
- [CMake](../build/CMake.md)
