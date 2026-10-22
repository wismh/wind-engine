# Continuous Integration

On your own machine, the normal way to get an SDK is the [Wind Launcher](https://github.com/wismh/wind-launcher/releases/latest): its SDKs page lists every released SDK for your system and installs one with a button. `setup-wind`, on this page, is for CI, where no one clicks.

A game's CI does not build the engine. It downloads the Wind editor SDK of the engine version your project names, and runs `wind_editor --batch --export` to build and export the game. This page shows how to get the SDK with `setup-wind`, two complete GitHub Actions workflows (Windows and Linux), and how to move a project to a newer engine.

---

## 1. Engine Versions

An engine version is `<major>.<minor>.<patch><stage>`:

| Version | Meaning |
| --- | --- |
| `0.1.7a` | Alpha: the eighth release of 0.1. Alpha is the current stage |
| `0.2.6b` | Beta |
| `1.0.0` | Production (no letter) |
| `0.2.7a-dev` | An SDK somebody built on their own machine. Never a release |

Every merge into the engine's `main` branch publishes a release, so the patch grows quickly (`0.1.99a` is followed by `0.1.100a`). The number goes on when the stage changes (`0.2.5a`, then `0.2.6b`). Versions are ordered by number, then alpha, beta, production.

Your project is tied to **one exact version**. `wind_project.toml` names it:

```toml
name = "My Game"
engine = "0.1.7a"
target = "my_game"
```

The editor and `--batch` refuse a project whose `engine` is not the SDK's version, character for character: `0.1.7` is not `0.1.7a`. Nothing updates the line for you, so your CI always builds with the engine you tested.

---

## 2. Getting the SDK: `setup-wind`

The engine repository has a composite action that downloads the SDK of your project's version from the engine's [Releases](https://github.com/wismh/wind-engine/releases), checks its SHA-256, unpacks it, and caches it:

```yaml
- uses: wismh/wind-engine/.github/actions/setup-wind@main
```

| Input | Default | Meaning |
| --- | --- | --- |
| `version` | `engine` in `wind_project.toml` | The version to install, like `0.1.7a` |
| `project-dir` | `.` | Where `wind_project.toml` is, when `version` is not given |
| `repo` | `wismh/wind-engine` | The repository with the releases |
| `symbols` | `false` | `true` also installs the debug symbols package (the `.pdb` files of the SDK, Windows only; ignored on Linux and macOS). Not needed to build or run; for a debugger or crash reports |
| `token` | `github.token` | Only to stay under the download rate limit; the releases are public |

After the step, `WIND_SDK_DIR` is the SDK root, `<sdk>/bin` (`wind_editor`, `wind-cli`) is on `PATH`, and the step outputs `sdk-dir` and `version`. The SDK is cached under the key `wind-sdk-<version>-<os>-<arch>` (`-symbols` is added to the key on Windows when `symbols` is `true`), so a run with a cache hit downloads nothing.

`@main` is the newest action. To pin it, use the release tag of the engine version you build with, `@v0.1.7a`: a release is a tag of the engine repository, with the action in it.

Without GitHub Actions, or in another CI, run the script the action runs. It needs Python 3.8 or newer and nothing else:

```bash
curl -fsSLO https://raw.githubusercontent.com/wismh/wind-engine/v0.1.7a/tools/ci/setup_sdk.py
python3 setup_sdk.py --project . --dest "$HOME/wind-sdk"
```

It prints the SDK directory on its last line. `--version 0.1.7a` names the version instead of reading the project; `--from-dir <directory>` takes the archive and its `.sha256` from a directory instead of downloading them.

The release assets are `wind-sdk-<version>-<os>-<arch>.zip` on Windows and `.tar.gz` on Linux and macOS, each with a `.sha256`. Platforms: `windows` and `linux` on x86_64, `macos` on arm64.

---

## 3. A Windows Workflow

`.github/workflows/export.yml`:

```yaml
name: Export

on:
  push:
    branches: [main]
  pull_request:

jobs:
  windows:
    runs-on: windows-2025-vs2026
    steps:
      - uses: actions/checkout@v4
      - uses: wismh/wind-engine/.github/actions/setup-wind@main
      - name: Export the game
        shell: pwsh
        run: |
          $out = Join-Path $PWD 'export'
          $log = Join-Path $PWD 'export.log'
          $p = Start-Process -Wait -PassThru -FilePath "$env:WIND_SDK_DIR\bin\wind_editor.exe" `
            -ArgumentList '--batch','--project',$PWD.Path,'--export',$out,'--log-file',$log
          Get-Content $log
          if ($p.ExitCode -ne 0) { exit $p.ExitCode }
      - uses: actions/upload-artifact@v4
        with:
          name: my-game-windows
          path: |
            export
            !export/**/*.pdb
```

The Release `wind_editor.exe` is a GUI program, so a shell would not wait for it: `Start-Process -Wait` does, and the log file carries its output. Use a Visual Studio 2026 runner: the SDK is built with its compiler, and a game module or export built with another one is refused (the build id covers the compiler). A checkout in `GITHUB_WORKSPACE` is fine.

---

## 4. A Linux Workflow

```yaml
name: Export

on:
  push:
    branches: [main]
  pull_request:

jobs:
  linux:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - name: Packages
        run: |
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends ninja-build \
            libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
            libxkbcommon-dev libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
            libegl1-mesa-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libibus-1.0-dev
      - uses: wismh/wind-engine/.github/actions/setup-wind@main
      - name: Export the game
        run: wind_editor --batch --project "$PWD" --export "$PWD/export" --log-file export.log
      - uses: actions/upload-artifact@v4
        if: always()
        with:
          name: my-game-linux
          path: |
            export
            export.log
```

The packages are the ones the SDK's own CI installs; the game links the engine statically with SDL3 and needs their headers. `--batch` opens no window. The exit code is `0` for an export, `1` when the SDK, the project, the build, or the copy failed (the last line of the log says why), and `2` for a usage error ([Desktop](../platforms/Desktop.md#2-exporting-without-a-window-ci-cloud-builds)). `--development` exports the Development Build (with the profiler and `wind-cli`) instead of `Release`.

macOS runners work the same way (`macos-14`, `.tar.gz`). The SDK ships player libraries there too, so the export links them and takes seconds; an older macOS SDK without `player/` compiles the engine instead, which takes minutes.

---

## 5. Updating the Engine Version

1. Pick the release on the engine's [Releases](https://github.com/wismh/wind-engine/releases) page.
2. Install that SDK on your machine: in the Wind Launcher's SDKs page press Install on that version (a project that needs a missing SDK shows **Install `<version>`** itself), or unpack the archive for your platform and Locate it.
3. Change `engine` in `wind_project.toml` to the new version, exactly as the release names it, and open the project with that SDK.
4. Fix what the engine changed in your code, play, and commit.

The next CI run installs the new SDK by itself (a new cache key), because `setup-wind` reads the version from the project. Do not change `engine` without these steps: a project is built with the engine it names, and a different engine can break your module's build.

Every release is a tag `v<version>`, so a version stays available. The engine's `sdk.toml` also records the commit it was built from.

---

## 6. Limits

- The SDK and your build must agree on the compiler: Visual Studio 2026 on Windows, the GCC of Ubuntu 24.04 on Linux, Apple Clang on `macos-14`. A runner image with a newer compiler may not load the older SDK until the engine publishes a new release.
- There is no Docker image.
- The releases are public. `setup-wind` needs no secret; its token only keeps the download under the rate limit.
- `setup-wind` and `wind_editor --batch --export` of the `templates/empty` project run on GitHub's Windows, Linux, and macOS runners against every release (the engine's `Setup Wind check` workflow), so the steps above work as written. The two complete workflows on this page are those steps with your project in place of the template; the exported game was built there, not started: an export check proves the build, not how the game plays.
