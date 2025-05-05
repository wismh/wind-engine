# Launcher

`wind_launcher` lists the projects you work on and the editor SDKs installed on this machine, and opens a project in the editor of the version it names. It is a Wind app like a game (`ENGINE_GAME(launcher::LauncherApp)`, `launcher/`), built against the static engine, so one launcher starts SDKs of every version. The plan is [Editor Plan](../architecture/Editor%20Plan.md#launcher).

## Get the launcher

```bash
cmake --preset vs-launcher
cmake --build build-launcher --config Release
cmake --install build-launcher --config Release --prefix out/launcher
out/launcher/bin/wind_launcher.exe
```

`ENGINE_LAUNCHER` ([CMake](../build/CMake.md#launcher-build)) builds it in the engine repo only, with the window backend and without `ENGINE_EDITOR`. The install puts `wind_launcher.exe`, its cooked `assets/`, and `assets/engine/` in `bin/`.

## Window

A header (Add project…, Locate editor…, status line), the Projects list, and the Editors list. Both lists are `ItemsControl` rows of a fixed 48px height.

| Row | Shows | Buttons |
| --- | --- | --- |
| Project | `name` and directory from `wind_project.toml`; "Editor 0.1.0" when an SDK of that version is known, "Needs 0.2.0" in red when not, "Cannot read" in red when the file is gone or broken | Open (enabled only with a matching SDK), Remove (drops the row; the files stay) |
| Editor | `version`; configuration, "local changes" when `dirty`, the first 8 hex digits of `commit`, and "installed" or "located"; the SDK root | Forget (located SDKs only) |

Add project opens the open-file dialog for `wind_project.toml` (filter `*.toml`) and puts the project first. Locate editor opens it for an SDK's `sdk.toml`, checks that it reads, and remembers that root. A wrong file name is a status-line message.

Buttons only record a `LauncherRequest`; the launcher's `Phase::Game` system acts on it, because acting rebuilds the rows and the row whose button ran would go away under the UI pass. Dialog answers are taken in the same system.

## Open

1. `sdk_for(sdks, project.engine)`: the first SDK of exactly that version. SDKs are sorted newest first, and for one version a clean SDK before a dirty one, so a tagged install wins over a dev SDK of the same version.
2. `IProcessLauncher::launch` ([Process](../modules/Process.md)) with `<sdk>/bin/wind_editor.exe --project <dir>`, started in `<sdk>/bin`. The editor runs on its own: closing the launcher leaves it running.
3. The project moves to the top of the list. A failed start says why in the status line (the executable is missing, it could not start).

The editor then checks the project's version against its own `sdk.toml` again ([Editor](Editor.md#start)).

## SDKs

- **Installed:** every `<user data>/sdks/*/sdk.toml`, where `<user data>` is `user_data_directory("Wind", "Launcher")` (`%APPDATA%\Wind\Launcher` on Windows). Install an SDK there with `cmake --install build-editor --config Release --prefix "%APPDATA%/Wind/Launcher/sdks/0.1.0"`.
- **Located:** roots remembered by Locate editor, for a dev SDK such as the engine repo's `out/sdk`. A located root that is also installed is listed once, as installed. One that no longer reads is skipped with a warning in the log.

Versions sort numerically per dot-separated part (`compare_versions`); a part that is not a number compares as text.

## Remembered state

`<user data>/launcher.txt`, UTF-8, one entry per line, rewritten on every change:

```
# Wind Launcher. Written by the launcher; one entry per line.
project=C:/games/tic-tac-toe
sdk=C:/engine/out/sdk
```

Projects are most recently opened or added first. Blank lines, `#` comments, and unknown lines are skipped, so an older launcher reads a newer file. Two paths are the same directory when they are equal after `absolute` and `lexically_normal`, without case on Windows. Without a user data directory nothing is remembered and no installed SDK is found; Locate still works for the session.

## Limits

- Windows only: `launch` and the editor are Windows today.
- No new project from a template and no download of SDK releases yet.
- Picking a project means picking its `wind_project.toml` file; there is no folder dialog.
- Like every Wind app the launcher renders continuously, without vsync.

## Tests

`launcher/tests/launcher_test.cpp` (`wind_launcher_tests`, built with `ENGINE_LAUNCHER` and tests): the state file (parse, format, save and load, a missing file), remembering and forgetting projects and SDKs, directory comparison, version order, finding installed and located SDKs with duplicates and broken roots, the SDK a version opens with, the editor command line, and reading a project entry. The window was checked by hand: lists, the disabled Open, Open starting the editor with the project (through `wind-cli click` in Debug), and the editor outliving the launcher.

## See also

- [Editor](Editor.md)
- [Project](../modules/Project.md)
- [Editor Plan](../architecture/Editor%20Plan.md)
