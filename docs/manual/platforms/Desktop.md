# Desktop

Wind natively targets Windows, Linux, and macOS desktops with OpenGL 3.3 Core rendering and SDL3 windowing.

A game that you open in the editor is built as a **module** (`my_game.dll`) that the editor loads on Play. The game you ship to players is a different build: a **standalone executable** with the engine linked in statically. The editor makes it for you with **Export**.

---

## 1. Exporting from the Editor

1. Open the project in the editor (from the launcher, or `wind_editor --project <dir>`).
2. Press **Export** next to Play. The log goes to the Build tab, and Cancel (the Play button during the build) stops it.
3. The status line says `Exported to <project>/export/<target>` when it is done.

Export always builds `Release`. The first export compiles the static engine and SDL, which takes minutes; later ones reuse `<project>/build-export/` and only rebuild your game. Your `CMakeLists.txt` does not change: it keeps `find_package(Wind REQUIRED)` and `engine_add_game`.

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

The project's `engine` version in `wind_project.toml` must equal the SDK's, as for Play. Cache `<project>/build-export/` between CI runs to skip the long first build. MSBuild refuses to build under a temporary directory (`MSB8029`), so do not put the project in `%TEMP%`.

---

## 3. Exporting by Hand

Export is two CMake commands; the editor runs the same ones:

```bash
cmake -S . -B build-export -DWIND_EXPORT=ON -DCMAKE_PREFIX_PATH=<sdk> -DWind_DIR=<sdk>/cmake \
      -DCMAKE_CONFIGURATION_TYPES=Release
cmake --build build-export --config Release --target my_game --parallel
```

`-DWIND_EXPORT=ON` makes `find_package(Wind)` add the SDK's engine source (`<sdk>/source`) instead of importing the SDK's shared `engine.dll`. The executable and its `assets/` end up in a directory that `build-export/wind/my_game.Release.export` names. Copy that directory, without the `.pdb`, `.ilk`, `.exp`, and `.lib` files, to ship it.

On Linux and macOS use a multi-config generator, as the editor does: add `-G "Ninja Multi-Config"` to the first command. Install the SDL build dependencies on Linux first (Ubuntu/Debian):

```bash
sudo apt-get install build-essential cmake ninja-build \
    libasound2-dev libpulse-dev libgl1-mesa-dev
```

macOS needs the Xcode Command Line Tools.

> [!NOTE]
> Export has been run end to end on Windows only. The Linux and macOS paths use the same code and the SDK builds there in CI, but an export on them has not been tried yet. On macOS check that the result is a complete application before you ship it.

---

## 4. Packaging for Distribution

An export directory is ready to ship as it is:
1. `my_game` (or `my_game.exe`)
2. `assets/` (the cooked assets: `catalog.toml`, your assets, and `assets/engine/`)
3. Nothing else: the engine and SDL are linked into the executable. The C++ runtime is not: on Windows the player needs the Microsoft Visual C++ Redistributable (the build uses the DLL runtime, `/MD`), and on Linux the system `libstdc++`/`libc++`, graphics, and audio libraries.

Zip the directory and send it. Keep the `.pdb` from `build-export/` if you want to symbolicate crash dumps.

---

## Next Steps

- Target web browsers with [Web (WebAssembly)](Web-Wasm.md).
- Package for mobile devices in [Android](Android.md). Web and Android are not exported from the editor; they still build from the SDK's engine source with Emscripten and Gradle.
