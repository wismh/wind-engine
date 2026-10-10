# Desktop

Wind natively targets Windows, Linux, and macOS desktops with OpenGL 3.3 Core rendering and SDL3 windowing.

A game that you open in the editor is built as a **module** (`my_game.dll`) that the editor loads on Play. The game you ship to players is a different build: a **standalone executable** with the engine linked in statically. The editor makes it for you with **Export**.

---

## 1. Exporting from the Editor

1. Open the project in the editor (from the launcher, or `wind_editor --project <dir>`).
2. Press **Export** next to Play. A dialog opens under the button:
   - **Folder**: where the game goes. It starts as `<project>/export/<target>` (then as the last folder you used in this editor session). **Browse...** opens the system folder picker. A relative path is taken from the project folder.
   - **Development Build**: off gives the build for players (`Release`). On gives a test build: your code unoptimized with symbols and asserts (`DebugGame`), and the engine with the UI profiler and the `wind-cli` server, so you can drive and profile the exported game like one running in the editor.
   - A line under them says why the folder cannot be used (the project itself, a folder that holds it, a non-empty folder that is not an earlier export), or warns that an earlier export is there and will be replaced.
   - **Export** starts the build (it is disabled while the folder is not usable, or while something else builds or plays); **Cancel** or a click outside closes the dialog.
3. The log goes to the Build tab, and Cancel (the Play button during the build) stops it.
4. The status line says `Exported to <folder>` when it is done.

The SDK ships the engine already built (the player libraries in `<sdk>/player/`: one for the Development build, one for Release), so an export only compiles your game and links: seconds, not minutes. Both builds share `<project>/build-export/`, so switching between them recompiles only your code. Your `CMakeLists.txt` does not change: it keeps `find_package(Wind REQUIRED)` and `engine_add_game`.

An SDK that has no player libraries (a Debug SDK, or macOS) builds the engine from its source instead: the first export takes minutes, and it is always a `Release` build. The Build tab's first lines say which of the two happened.

The target directory is emptied by every export, so Export only does that to a directory that does not exist, is empty, or already holds a `.wind_export` file from an earlier export. Any other directory is refused and nothing in it is deleted. Add `/export/` to the project's `.gitignore` (the template already ignores `/build*/`).

---

## 2. Exporting Without a Window (CI, Cloud Builds)

`wind_editor --batch` does the same export without opening a window, an audio device, or a GPU context:

```bash
wind_editor --batch --project path/to/my_game --export path/to/out --log-file export.log
```

| Option | Meaning |
| --- | --- |
| `--batch` | Run a command and exit; no window. Needs a command (`--export`) |
| `--project <dir>` | The project (the directory with `wind_project.toml`). Required |
| `--export [<dir>]` | Export to `<dir>`; `<project>/export/<target>` when the directory is left out |
| `--development` | With `--export`: the Development Build (`DebugGame`, with the profiler and `wind-cli`) instead of `Release`. Needs a Release SDK |
| `--log-file <path>` | Also write every output line to this file |

| Exit code | Meaning |
| --- | --- |
| `0` | Exported |
| `1` | The SDK, the project, the build, or the copy failed (the last output line says why) |
| `2` | Usage error |

On Windows the Release `wind_editor.exe` is a GUI program: a shell does not wait for it and shows no output. Wait for it and read the log file instead:

```powershell
$p = Start-Process -Wait -PassThru -FilePath "$sdk\bin\wind_editor.exe" `
    -ArgumentList '--batch','--project',$project,'--export',$out,'--log-file',$log
if ($p.ExitCode -ne 0) { Get-Content $log; exit $p.ExitCode }
```

The project's `engine` version in `wind_project.toml` must equal the SDK's, character for character (`0.1.7a`, not `0.1.7`), as for Play. A CI gets the SDK of that version from the engine's Releases: [Continuous Integration](../getting-started/Continuous-Integration.md). With the SDK's player libraries an export takes seconds, so there is nothing to cache between CI runs except `<project>/build-export/` if you want your own code compiled incrementally. MSBuild refuses to build under a temporary directory (`MSB8029`), so do not put the project in `%TEMP%`.

---

## 3. Exporting by Hand

Export is two CMake commands; the editor runs the same ones. The build directory has both configurations, `DebugGame` (the Development Build) and `Release`:

```bash
cmake -S . -B build-export -DWIND_EXPORT=ON -DCMAKE_PREFIX_PATH=<sdk> -DWind_DIR=<sdk>/cmake \
      "-DCMAKE_CONFIGURATION_TYPES=DebugGame;Release"
cmake --build build-export --config Release --target my_game --parallel
```

`-DWIND_EXPORT=ON` makes `find_package(Wind)` link the SDK's static player libraries (`<sdk>/player/Development` for `DebugGame`, `<sdk>/player/Shipping` for `Release`) instead of importing the SDK's shared `engine.dll`, or add the SDK's engine source (`<sdk>/source`) when the SDK has no players. `--config DebugGame` builds the Development Build. The executable and its `assets/` end up in a directory that `build-export/wind/my_game.<config>.export` names. Copy that directory, without the `.pdb`, `.ilk`, `.exp`, and `.lib` files, to ship it.

On Linux and macOS use a multi-config generator, as the editor does: add `-G "Ninja Multi-Config"` to the first command. Install the SDL build dependencies on Linux first (Ubuntu/Debian):

```bash
sudo apt-get install build-essential cmake ninja-build \
    libasound2-dev libpulse-dev libgl1-mesa-dev
```

macOS needs the Xcode Command Line Tools.

> [!NOTE]
> Export has been run end to end on Windows only, and the player libraries built there. The Linux path builds them the same way (the SDK install runs the same script with its Ninja generator) but has not been tried; macOS has no player libraries, so an export there builds the engine from the SDK source. On macOS check that the result is a complete application before you ship it.

---

## 4. Packaging for Distribution

An export directory is ready to ship as it is:
1. `my_game` (or `my_game.exe`)
2. `assets/` (the cooked assets: `catalog.toml`, your assets, and `assets/engine/`)
3. Nothing else: the engine and SDL are linked into the executable. The C++ runtime is not: on Windows the player needs the Microsoft Visual C++ Redistributable (the build uses the DLL runtime, `/MD`), and on Linux the system `libstdc++`/`libc++`, graphics, and audio libraries.

Zip the directory and send it. The `Release` build has no `.pdb` of its own (the engine is built without symbols); a Development Build has symbols for your code and the engine in `build-export/bin/DebugGame/` if you want to debug it.

---

## Next Steps

- Target web browsers with [Web (WebAssembly)](Web-Wasm.md).
- Package for mobile devices in [Android](Android.md). Web and Android are not exported from the editor; they still build from the SDK's engine source with Emscripten and Gradle.
