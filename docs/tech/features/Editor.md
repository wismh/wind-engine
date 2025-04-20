# Editor

`wind_editor` is the editor host. It is built only with `ENGINE_EDITOR` (preset `vs-editor`, or a game's `editor` preset), from `editor/`. It is an engine client like a game: it includes only `<engine/...>` and links the shared `engine`. Today it picks a game module, plays it in `kPrimaryWindow`, and stops it. The UI Inspector and UI Profiler still open their own windows from the game; moving them into the editor is wind-170. The plan is [Editor Plan](../architecture/Editor%20Plan.md).

## Start

`EditorApp::start` (`editor/src/editor_app.cpp`):

1. `EngineHost::init`, then `open_primary` with `WindowDesc{"Game", 800x600}`. That is `kPrimaryWindow`, the game's window.
2. `load_catalog(assets_root() / "editor")`: the editor's own `catalog.toml`. A failure is fatal.
3. `Worlds::add` for the editor world, `open_window({"Wind Editor", 1280x800})`, `bind_window`, `enable_ui`, and one `UiCanvas` (`FillWindow`) with `assets/ui/editor.xml` and `assets/css/editor.css` over `EditorViewModel`.
4. `purge_game_module_copies(<user data>/live)`. The user data directory is `user_data_directory("Wind", "Editor")`. Without one the copies go to `<temp>/wind_editor/live`.
5. `--game <module>` sets the game and skips the dialog. Otherwise the editor opens the open-file dialog (filter `*.dll`, `*.dylib`, or `*.so`). Nothing is remembered between runs.

Command line:

| Option | Effect |
| --- | --- |
| `--game <module>` | Use this module. No dialog at start |
| `--play` | Press Play when the loop starts. Needs `--game` |

## Window

A toolbar over an empty content area (the Inspector and Profiler panels go there in wind-170).

| Control | Binding | Does |
| --- | --- | --- |
| Choose game… | `chooseGame` | Opens the dialog. Disabled while playing |
| Play / Stop | `togglePlay`, `playLabel`, `isPlaying` (`checked`, red while playing) | Play when idle, Stop while playing. Disabled with no game |
| Status line | `statusText` | Ready, Playing, Stopped, the game quit, or why Play failed |
| Game path | `gamePath` | The chosen module |

The commands are `MethodCommand` (`editor/src/method_command.h`) bound to `Toolbar` methods. A button only records an `EditorRequest`. The dialog answer (`FileDialogResultEvent`) and the editor window's `WindowCloseRequestedEvent` are read by one editor-world system that also only records. Every transition runs in `RunHooks::on_frame_end`, after the frame drew, because Play and Stop create and destroy worlds that no system of that frame may still be walking.

## Play

`PlaySession::play` (`editor/src/play_session.cpp`):

1. `load_game_module(module, live_root)`: copy the module and its `.pdb` into `live/<n>/`, load the copy, resolve the three exports, compare the build id ([Core](../modules/Core.md)). An error is shown in the status line and the editor stays idle.
2. `load_catalog(<module dir>/assets)`. An error unloads the module.
3. Remember the worlds and windows that exist now.
4. `wind_create_game(services)`.
5. Apply the game's `primary_window()` to `kPrimaryWindow`: title, size, position when set, borderless, always on top. `transparent` cannot change after creation; the status line says so. `resizable` and `maximized` are not applied.
6. `EngineHost::attach_game` (icon, bind, UI, audio, size), then `on_start`.

## Stop

`PlaySession::stop`. Nothing built from game code may survive the unload at the end:

1. `on_quit`.
2. `EngineHost::detach_game`: unbind `kPrimaryWindow`, clear its command buffer (its `CmdDrawUI` entries point into game documents), reset its NanoVG context and register `builtin::font_ui` again, clear drag region and click-through, overlay mode `Auto`, `paused` false.
3. Destroy every world that did not exist before Play.
4. Close every window that was not open before Play.
5. `InputSystem::reset`, then `IAudioSystem::stop_all`.
6. `unload_catalog(<module dir>/assets)`.
7. `wind_destroy_game`.
8. Destroy the `GameModule`: unload the copy and delete `live/<n>/`.
9. Put back the idle title and style of `kPrimaryWindow`. Its size stays.

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

The editor's assets and catalog land in `bin/assets/editor/` (`ENGINE_RUNTIME_ASSETS_DIR`, see [CMake](../build/CMake.md)). In a game repo's editor build, `wind_editor`, `engine.dll`, the game module, and the game's `assets/catalog.toml` share one `bin/`.

## Limits

- `engine.dll` is loaded once and stays locked while the editor runs. An engine change needs an editor restart and a game rebuilt against it. A module with another build id is refused with both ids in the status line.
- The game runs in the editor's process. A crash in game code, including code that keeps running after a fatal report, takes the editor down.
- `transparent`, `resizable`, and `maximized` of the game window are fixed at editor start.
- The game's window icon stays on `kPrimaryWindow` after Stop.
- No project list and no remembered game.

## Tests

`editor/tests/play_session_test.cpp` (`wind_editor_tests`) drives `PlaySession` with headless services (`tests/fixtures/fake_services.h`), a recording `IPlayHost`, and the fixture module: Play applies the window and attaches, Stop runs in the order above and deletes the copy, Play again works, a wrong build id and a catalog error leave nothing behind, the destructor stops. `tests/game_module_test.cpp` covers the loader.

## See also

- [Editor Plan](../architecture/Editor%20Plan.md)
- [Core](../modules/Core.md)
- [Windowing](Windowing.md)
- [CMake](../build/CMake.md)
