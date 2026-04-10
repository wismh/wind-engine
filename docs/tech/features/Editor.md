# Editor

`wind_editor` is the editor host. It is built only in the engine repo with `ENGINE_EDITOR` (preset `vs-editor`), from `editor/`, and shipped to games as the editor SDK. It is an engine client like a game: it includes only `<engine/...>` and links the shared `engine`. It picks a game module, plays it in `kPrimaryWindow`, inspects and profiles its UI, and stops it. The plan is [Editor Plan](../architecture/Editor%20Plan.md).

## Get the editor

From the engine repo:

```bash
cmake --preset vs-editor
cmake --build build-editor --config Release
cmake --install build-editor --config Release --prefix out/sdk
out/sdk/bin/wind_editor.exe --game path/to/my_game.dll --play
```

The SDK's `bin/` holds `wind_editor.exe`, `engine.dll`, `assets/engine/`, and `assets/editor/`, so it runs from there. The editor writes its `game.log` beside itself. A game builds its module against the same SDK (`WIND_EDITOR_SDK`, [Game Consumer](../build/Game%20Consumer.md#editor-module)), so the build ids match. Layout: [CMake](../build/CMake.md#editor-sdk).

## Start

`EditorApp::start` (`editor/src/editor_app.cpp`):

1. `EngineHost::init`, then `open_primary` with `WindowDesc{"Game", 800x600}`. That is `kPrimaryWindow`, the game's window.
2. `load_catalog(assets_root() / "editor")`: the editor's own `catalog.toml`. A failure is fatal.
3. `Worlds::add` for the editor world, `open_window({"Wind Editor", 1280x800})`, `bind_window`, `enable_ui`, and one `UiCanvas` (`FillWindow`) with `assets/ui/editor.xml` and `assets/css/editor.css` over `EditorViewModel`. `EditorPanels` spawns two more canvases (`Fixed`, `order` 1): `assets/ui/inspector.xml` and `assets/ui/profiler.xml`, both with `assets/css/panels.css`.
4. `purge_game_module_copies(<user data>/live)`. The user data directory is `user_data_directory("Wind", "Editor")`. Without one the copies go to `<temp>/wind_editor/live`.
5. `--game <module>` sets the game and skips the dialog. Otherwise the editor opens the open-file dialog (filter `*.dll`, `*.dylib`, or `*.so`). Nothing is remembered between runs.

Command line:

| Option | Effect |
| --- | --- |
| `--game <module>` | Use this module. No dialog at start |
| `--play` | Press Play when the loop starts. Needs `--game` |

## Window

A toolbar, a tab strip, and the active panel.

| Control | Binding | Does |
| --- | --- | --- |
| Choose game… | `chooseGame` | Opens the dialog. Disabled while playing |
| Play / Stop | `togglePlay`, `playLabel`, `isPlaying` (`checked`, red while playing) | Play when idle, Stop while playing. Disabled with no game |
| Status line | `statusText` | Ready, Playing, Stopped, the game quit, or why Play failed |
| Game path | `gamePath` | The chosen module |
| Inspector / Profiler tabs | `showInspector`, `showProfiler`, `inspectorTab`, `profilerTab` (`checked`) | Show that panel. Inspector is first |

The commands are `MethodCommand` (`editor/src/method_command.h`) bound to `Toolbar` methods. Choose game and Play/Stop only record an `EditorRequest`. A tab button switches the tab at once. The dialog answer (`FileDialogResultEvent`) and the editor window's `WindowCloseRequestedEvent` are read by one editor-world system that also only records. Every transition runs in `RunHooks::on_frame_end`, after the frame drew, because Play and Stop create and destroy worlds that no system of that frame may still be walking.

## Panels

`EditorPanels` (`editor/src/editor_panels.cpp`) owns `InspectorPanel` and `ProfilerPanel`. One editor-world system in `Phase::Game` places the canvases and refreshes the visible panel. `Game`, not `Bind`: `run_bind` of the editor world must see this frame's copy. The active panel's canvas covers the window below `kPanelTop` (88px: the 56px toolbar and the 32px tab strip of `editor.css`). The other is `Fixed` with an empty rect, so it takes no clicks.

Each panel holds a pointer to the game world only between attach and detach. Its rows are plain copies of the engine's snapshot rows. Details: [UI Inspector](UI%20Inspector.md), [UI Profiler](UI%20Profiler.md).

Both panels inspect the world bound to `kPrimaryWindow`. A game with a second world (a tool window) shows only its primary world.

## Play

`PlaySession::play` (`editor/src/play_session.cpp`):

1. `load_game_module(module, live_root)`: copy the module and its `.pdb` into `live/<n>/`, load the copy, resolve the three exports, compare the build id ([Core](../modules/Core.md)). An error is shown in the status line and the editor stays idle.
2. `load_catalog(<module dir>/assets)`. An error unloads the module.
3. Remember the worlds and windows that exist now.
4. `wind_create_game(services)`.
5. Apply the game's `primary_window()` to `kPrimaryWindow`: title, size, position when set, borderless, always on top. `transparent` cannot change after creation; the status line says so. `resizable` and `maximized` are not applied.
6. `EngineHost::attach_game` (icon, bind, UI, audio, size), then `on_start`.
7. `IPlayHost::attach_tools` with the world of `kPrimaryWindow`: `set_inspector_attached` and `set_ui_profiler_attached` on that world. Pick starts off, so the game gets its clicks.

## Stop

`PlaySession::stop`. Nothing built from game code may survive the unload at the end:

1. `IPlayHost::detach_tools`: detach the inspector and profiler from the game world and clear both panels. The game world still exists and is still bound.
2. `on_quit`.
3. `EngineHost::detach_game`: unbind `kPrimaryWindow`, clear its command buffer (its `CmdDrawUI` entries point into game documents), reset its NanoVG context and register `builtin::font_ui` again, clear drag region and click-through, overlay mode `Auto`, `paused` false.
4. Destroy every world that did not exist before Play.
5. Close every window that was not open before Play.
6. `InputSystem::reset`, then `IAudioSystem::stop_all`.
7. `unload_catalog(<module dir>/assets)`.
8. `wind_destroy_game`.
9. Destroy the `GameModule`: unload the copy and delete `live/<n>/`.
10. Put back the idle title and style of `kPrimaryWindow`. Its size stays.

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
- No project list and no remembered game.
- The panels show only the world of `kPrimaryWindow`.

## Tests

`editor/tests/play_session_test.cpp` (`wind_editor_tests`) drives `PlaySession` with headless services (`tests/fixtures/fake_services.h`), a recording `IPlayHost`, and the fixture module: Play applies the window and attaches the game and then the tools to the game world, Stop runs in the order above (tools first, while the game world is still bound) and deletes the copy, Play again works, a wrong build id and a catalog error leave nothing behind, the destructor stops. `editor/tests/editor_panels_test.cpp` covers the tabs and canvas placement; `inspector_panel_test.cpp`, `profiler_panel_test.cpp`, and `profiler_chart_test.cpp` cover the panels. `wind_editor_tests` compiles every editor source except `main.cpp` and `editor_app.cpp`, with wind_editor's generated `asset_ids.h`. `tests/game_module_test.cpp` covers the loader.

## See also

- [Editor Plan](../architecture/Editor%20Plan.md)
- [Core](../modules/Core.md)
- [Windowing](Windowing.md)
- [CMake](../build/CMake.md)
