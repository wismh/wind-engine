# Launcher

`wind_launcher` lists the projects you work on and the editor SDKs installed on this machine, makes a new project from an SDK's template, and opens a project in the editor of the version it names. It is a Wind app like a game (`ENGINE_GAME(launcher::LauncherApp)`, `launcher/`), built against the static engine, so one launcher starts SDKs of every version. The plan is [Editor Plan](../architecture/Editor%20Plan.md#launcher).

## Get the launcher

```bash
cmake --preset vs-launcher
cmake --build build-launcher --config Release
cmake --install build-launcher --config Release --prefix out/launcher
out/launcher/bin/wind_launcher.exe
```

`ENGINE_LAUNCHER` ([CMake](../build/CMake.md#launcher-build)) builds it in the engine repo only, with the window backend and without `ENGINE_EDITOR`. The install puts `wind_launcher.exe`, its cooked `assets/`, and `assets/engine/` in `bin/`.

## Window

A vertical navbar on the left (Projects, SDKs) picks the page shown beside it; New project is a third page with no navbar entry, reached from Projects (Projects stays checked); a status bar runs under the page. The navbar buttons are bound `checked`, and each page's `display` comes from the view-model through `var-display` (`block` or `none`). Both lists are `ItemsControl` rows of a fixed 48px height.

| Page | Header | Row shows | Row buttons |
| --- | --- | --- | --- |
| Projects | Add existing…, New project… | `name` and directory from `wind_project.toml`; "SDK 0.1.0" when an SDK of that version is known, "Needs SDK 0.2.0" in red when not, "Cannot read" in red when the file is gone or broken | Open (enabled only with a matching SDK), Remove (drops the row; the files stay) |
| SDKs | the install directory, Open folder, Locate… | `version`; configuration, "local changes" when `dirty`, the first 8 hex digits of `commit`, and "installed" or "located"; the SDK root | ··· menu |

The ··· button anchors two `Popup`s ([UI](../modules/UI.md#popup)), both `bottom-end`: the menu, and the delete confirmation that replaces it. The menu has Show in Explorer, then Forget for a located SDK or Delete… for an installed one. Forget drops the remembered root and leaves the files. Delete… opens the confirmation (the version and the root, Cancel, Delete). Clicking ··· again closes whichever is open. Which popup is open is row view-model state (`menuOpen`, `confirmOpen`).

Add existing opens the open-file dialog for `wind_project.toml` (filter `*.toml`) and puts the project first. Locate opens it for an SDK's `sdk.toml`, checks that it reads, and remembers that root. A wrong file name is a status-line message. Open folder creates the install directory if it is missing and shows it in the file manager.

Buttons that change the lists only record a `LauncherRequest`; the launcher's `Phase::Game` system acts on it, because acting rebuilds the rows and the row whose button ran would go away under the UI pass. Dialog answers are taken in the same system. Switching pages and opening a popup change only view-model fields and happen at once.

## New project

New project… fills the page and shows it:

| Field | Starts as | Control |
| --- | --- | --- |
| Name | "My Game", or "My Game 2" and on while that directory is taken | `TextInput` |
| Location | `location=` from `launcher.txt`, else `default_project_location()`: `%USERPROFILE%/WindProjects` (`$HOME/WindProjects` elsewhere) | `TextInput` and Browse… (`IWindowControl::request_open_folder`, [Windowing](Windowing.md)) |
| SDK | the newest SDK with a template, else the newest | a button whose `Popup` lists every SDK (`sdkOptions`), "no template" on those without one |

Typing has no change event, so the launcher's frame system checks the fields every frame the page is shown (`check_new_project`) and writes the hint under them: "Creates `<location>/<name>`, build target `<target>`, and opens it in the editor", or in red why not. Create, and Return in a field, works only without a problem; Cancel goes back to Projects.

`new_project_problem` refuses an empty name; one with `< > : " / \ | ? *` or a control character; one that starts with a space or ends with a space or a dot; a Windows device name (`CON`, `COM1.txt`, …); one without an ASCII letter or digit (no target); an empty or relative location; on Windows a project directory longer than `kMaxProjectPath` (100) characters, because MSBuild writes files about 150 characters deep under `build-editor/` and fails past `MAX_PATH`; and a project directory that exists and is not empty. The launcher adds: no SDK, or an SDK without `templates/empty`.

The target is `project_target(name)`: lower-case ASCII letters and digits, every other run one `-`, `game-` before a leading digit ("My Game 2" is `my-game-2`).

Create (`create_project`) copies `<sdk>/templates/empty/` into `<location>/<name>/`, making both, and replaces `{{name}}`, `{{target}}`, `{{engine}}` (the SDK's version), and `{{sdk}}` (its root, `/` separators) in every file. A valid name has no `"` or `\`, so it goes into TOML and C++ string literals unescaped. A failure removes the directory it made and says why in the status line. Then the location is remembered (`location=`), the project goes first in the list, the page goes back to Projects, and the project opens as with Open, but in the SDK picked: Open alone would prefer a clean SDK of that version over a dirty dev one.

### Template

The template lives in the engine repo's `templates/empty/`, and the SDK installs it to `<sdk>/templates/` ([CMake](../build/CMake.md#editor-sdk)), so a project starts from the API of the SDK it builds against. An SDK installed before wind-183 has none. It is the Quickstart's game with one window and no systems:

| File | |
| --- | --- |
| `wind_project.toml` | name, engine, target |
| `CMakeLists.txt` | `find_package(Wind)`, `engine_add_game(<target> src/...)` |
| `src/main.cpp`, `src/game.h`, `src/game.cpp` | `game::Game : GameBase`, a 1280×720 window titled with the name |
| `assets/.gitkeep` | `assets/` must exist: Play loads the module's `assets/catalog.toml` |
| `CMakePresets.json`, `CMakeUserPresets.json` | for an IDE: the `editor` preset (`DebugGame;Release`), and `editor-local` with `CMAKE_PREFIX_PATH` set to the SDK. The editor does not need them |
| `.gitignore` | `build*/`, `.vs/`, `CMakeUserPresets.json` |

## Open

1. `sdk_for(sdks, project.engine)`: the first SDK of exactly that version. SDKs are sorted newest first, and for one version a clean SDK before a dirty one, so a tagged install wins over a dev SDK of the same version.
2. `IProcessLauncher::launch` ([Process](../modules/Process.md)) with `<sdk>/bin/wind_editor.exe --project <dir>`, started in `<sdk>/bin`. The editor runs on its own: closing the launcher leaves it running.
3. The project moves to the top of the list. A failed start says why in the status line (the executable is missing, it could not start).

The editor then checks the project's version against its own `sdk.toml` again ([Editor](Editor.md#start)).

## SDKs

- **Installed:** every `<install>/*/sdk.toml`, where `<install>` is `sdk_install_directory()`: `%LOCALAPPDATA%\Programs\Wind\Sdks` on Windows (per user, not roaming, writable without elevation), `$XDG_DATA_HOME/Wind/Sdks` or `~/.local/share/Wind/Sdks` elsewhere. The subdirectory name does not matter, the version comes from `sdk.toml`. Copy an SDK there by hand, or install one with `cmake --install build-editor --config Release --prefix "%LOCALAPPDATA%/Programs/Wind/Sdks/0.1.0"`. A subdirectory whose name starts with `.` is skipped. Without the environment variable there is no install directory and only located SDKs are listed.
- **Located:** roots remembered by Locate, for a dev SDK such as the engine repo's `out/sdk`. A located root that is also installed is listed once, as installed. One that no longer reads is skipped with a warning in the log.

Versions sort numerically per dot-separated part (`compare_versions`); a part that is not a number compares as text.

### Delete

`delete_sdk` acts only on an installed SDK. It renames the root to `.deleting-<name>` beside it, then removes that. Windows refuses the rename while a file inside is open without delete sharing (an editor running from the SDK), so such an SDK stays whole and the status line asks to close the editor. A removal that stops after the rename leaves a `.deleting-` directory that `find_sdks` skips and `remove_deleted_sdks` clears when the launcher starts.

## Remembered state

`<user data>/launcher.txt`, UTF-8, one entry per line, rewritten on every change:

```
# Wind Launcher. Written by the launcher; one entry per line.
project=C:/games/tic-tac-toe
sdk=C:/engine/out/sdk
location=C:/Users/me/WindProjects
```

Projects are most recently opened or added first. `location=` is where the last new project was made; it is absent until then. Blank lines, `#` comments, and unknown lines are skipped, so an older launcher reads a newer file. Two paths are the same directory when they are equal after `absolute` and `lexically_normal`, without case on Windows. Without a user data directory nothing is remembered; Locate still works for the session.

## Limits

- Desktop only: `launch` runs on Windows, Linux, and macOS ([Process](../modules/Process.md)); the editor SDK builds on all three ([CMake](../build/CMake.md#linux-and-macos)).
- One template (`empty`), and no download of SDK releases yet.
- Picking a project means picking its `wind_project.toml` file; there is no folder dialog.
- Like every Wind app the launcher renders continuously, without vsync.

## Tests

`launcher/tests/launcher_test.cpp` (`wind_launcher_tests`, built with `ENGINE_LAUNCHER` and tests): the state file (parse, format, save and load, a missing file), remembering and forgetting projects and SDKs, directory comparison, version order, the install directory, finding installed and located SDKs with duplicates, broken roots, and dot directories, deleting an SDK (its neighbours stay, one in use stays whole, leftovers are cleared), the SDK a version opens with, the editor and file manager command lines, reading a project entry, `location=`, the target name, every refusal of `new_project_problem`, making a project from a template (placeholders, a taken directory, a missing template), and the engine's own `templates/empty` leaving no placeholder. The window was checked by hand through `wind-cli` in Debug: lists, the disabled Open, Open starting the editor with the project, the editor outliving the launcher, the navbar switching pages, the ··· menu (Show in Explorer, Forget, Delete… and its confirmation).

## See also

- [Editor](Editor.md)
- [Project](../modules/Project.md)
- [Editor Plan](../architecture/Editor%20Plan.md)
