# Release

Every merge into `main` is a release: a numbered editor SDK for Windows, Linux, and macOS, published as a GitHub Release a game's CI can download. The version scheme is Unity's: `<major>.<minor>.<patch><stage>`.

## Version

| Part | Where it comes from |
| --- | --- |
| `major`, `minor` | `ENGINE_VERSION_MAJOR`, `ENGINE_VERSION_MINOR` in the root `CMakeLists.txt`. A person raises them |
| `stage` | `ENGINE_VERSION_STAGE`: `"a"` alpha, `"b"` beta, `""` production. A person changes it |
| `patch` | `ENGINE_VERSION_PATCH`, a CMake cache entry the release workflow passes (`-DENGINE_VERSION_PATCH=N`). Counted by CI |

`0.1.7a` is the eighth alpha of 0.1 (patch 0 is the first). `0.1.7b` is the same number as a beta, `0.1.7` as production. There is no upper limit on the patch: `0.1.99a` is followed by `0.1.100a`, and only a person starting `0.2.0a` ends the line. The stage does not reset the patch: after `0.2.5a` the first beta is `0.2.6b`, not `0.2.5b` (tags are unique by number). A new minor starts again at patch 0.

Without `ENGINE_VERSION_PATCH` (every build outside the release workflow) the build is local: its version is `0.2.10-dev`. CMake prints it (`Wind engine 0.2.10-dev`) and the SDK installed from it says so in `sdk.toml`. A release keeps `ENGINE_VERSION_STAGE`. `ENGINE_VERSION_PATCH` must be a whole number without leading zeros, or the configure fails; `ENGINE_VERSION_STAGE` must be `a`, `b`, or empty.

Order (`engine::EngineVersion`, [Project](../modules/Project.md#engine-version)): the number `major.minor.patch` first, numerically. For one number a `-dev` build comes before every release of it, then alpha, beta, production:

```
0.1.99a < 0.1.100a < 0.2.0a-dev < 0.2.0a < 0.2.0b < 0.2.0 < 0.2.1a
```

The launcher sorts its SDK list by this order. Whether a project and an SDK fit each other is not an order question: a project's `engine` in `wind_project.toml` must be the SDK's `sdk.toml` `version` as exact text, `0.1.7a` and `0.1.7` are different engines. A project moves to a new engine by editing that line (the Continuous Integration page of the manual, [Updating the engine](../../manual/getting-started/Continuous-Integration.md#updating-the-engine-version)).

CMake itself sees only the three numbers: `project(engine VERSION 0.1.7)` is `PROJECT_VERSION`, and `WindConfigVersion.cmake` accepts `find_package(Wind 0.1.7)` for the `0.1.7a`, `0.1.7b`, and `0.1.7` SDKs alike, as it cannot tell stages apart. The full text is `ENGINE_VERSION_FULL`, `Wind_VERSION_FULL` in a game, and `sdk.toml` `version` ([CMake](CMake.md#version)).

## What the release workflow does

`.github/workflows/release.yml`, on every push to `main` (not on tags, not on branches) and by hand (`workflow_dispatch`, below), one run at a time (`concurrency: release`, no cancel of a run in progress; while one run builds, only the newest later push waits, so a burst of merges may release only its last commit).

1. **version**: `tools/ci/next_version.py` reads the major, minor, and stage from `CMakeLists.txt` and the tags `v<major>.<minor>.*` from the checkout (`fetch-depth: 0`), and answers `<major>.<minor>.<highest patch + 1><stage>` (patch 0 without tags). It recognizes tags of the form `v<EngineVersion>` (`v0.1.7a`) and ignores any other.
2. **sdk**, on `ubuntu-24.04` (x86_64), `macos-15` (arm64), and `windows-2025-vs2026` (x86_64): the same configure (the `linux-editor` / `macos-editor` / `vs-editor` preset; on Windows `tools/ci/msvc_env.ps1` loads the MSVC environment first, then Ninja Multi-Config builds with `cl`), build, and `ctest` as `sdk.yml`, with `-DENGINE_VERSION_PATCH=<patch>`; then `cmake --install build-editor --config Release --prefix out/sdk --component Unspecified`: the SDK without the install components `symbols` (the `.pdb` files, Windows) and `source` (`<sdk>/source`). The install builds the player libraries on all three systems (`ENGINE_SDK_PLAYER`). On Windows `--component symbols --prefix out/symbols` puts the `.pdb` files in the symbols tree, and on every system `tools/ci/split_symbols.py` takes the rest of the debug information out of the SDK into it (below); the job checks that the SDK has no `.pdb` and no `source/`. On Linux the job also installs `--component source --prefix out/source`. The job checks that `sdk.toml` has the version, `dirty = false`, and the platform; on Linux and macOS it runs `tools/ci/sdk_smoke.sh` (a game against the stripped SDK, and the editor playing it). Then it packs `out/sdk` as a directory `wind-sdk-<version>-<os>-<arch>` into an archive and writes its SHA-256, and the same for `out/symbols` (`wind-sdk-symbols-<version>-<os>-<arch>`) and, on Linux, `out/source` (`wind-sdk-source-<version>.tar.gz`).
3. **publish**, only when all three `sdk` jobs passed: `gh release create v<version> --target <sha> --title "Wind <version>" --generate-notes` with the fourteen files (three SDKs, their three symbols packages, and the source, each with its `.sha256`). Alpha and beta are marked pre-release; a production version is `--latest`. This job alone has `contents: write`.

A failed `sdk` job leaves no tag and no release; the next push counts the same patch again. The three `sdk` jobs do not cancel each other (`fail-fast: false`), so one run shows what is broken on every system.

**Dry run.** `gh workflow run release.yml --ref <branch> -f publish=false` (the input `publish` is `false` by default) runs the version job and the three `sdk` jobs on any branch and skips `publish`: the same build, tests, install, checks, pack, and upload as a release, without a tag or a Release. The version it prints is the one the branch would get if it were merged now. Iterate on a branch with it and merge when it is green; every merge to `main` is a real release.

**Setup Wind check.** `.github/workflows/setup-wind-check.yml` runs after every successful `Release` (and by hand, with an optional `version`, newest release by default). On Linux, macOS, and Windows it installs that released SDK with `.github/actions/setup-wind`, checks `sdk.toml`, creates a project from `templates/empty`, and runs `wind_editor --batch --project <dir> --export <out> --log-file <log>` (on Windows with `Start-Process -Wait -PassThru`, since the editor is a GUI-subsystem program), then checks the exit code 0 and that the exported executable exists. It is the test of the whole path a game's CI takes.

## Assets

| `<os>` | `<arch>` | Archive | Also |
| --- | --- | --- | --- |
| `windows` | `x86_64` | `wind-sdk-<version>-windows-x86_64.zip` | `.sha256` |
| `linux` | `x86_64` | `wind-sdk-<version>-linux-x86_64.tar.gz` | `.sha256` |
| `macos` | `arm64` | `wind-sdk-<version>-macos-arm64.tar.gz` | `.sha256` |
| `windows` | `x86_64` | `wind-sdk-symbols-<version>-windows-x86_64.zip` | `.sha256` |
| `linux` | `x86_64` | `wind-sdk-symbols-<version>-linux-x86_64.tar.gz` | `.sha256` |
| `macos` | `arm64` | `wind-sdk-symbols-<version>-macos-arm64.tar.gz` | `.sha256` |
| all | | `wind-sdk-source-<version>.tar.gz` | `.sha256` |

**Symbols package.** The debug information is not in the SDK archive: it is only needed in a debugger or for a crash report, and it is most of the SDK's size. `tools/ci/split_symbols.py` splits it off an installed SDK into a symbols tree; the archive is that tree with one top directory like the SDK one and `symbols.toml` (`version`, `build_id`, `platform` copied from `sdk.toml`), laid out so that unpacking it over the SDK of the same build puts everything back:

| | Windows | Linux | macOS |
| --- | --- | --- | --- |
| `bin/` | `*.pdb` | `<binary>.debug` (the binary is stripped and keeps a `gnu-debuglink`) | `<binary>.dSYM` (the binary is stripped and signed again) |
| `player/<flavour>/lib/*` | the original `.lib` with `/Z7` debug information; the SDK has it stripped (`llvm-strip -g`) | the original `.a` with `-g`; the SDK has it stripped (`strip --strip-debug`) | the same with `strip -S` |

A static library is split only if stripping saves a megabyte and a fifth of it, so in practice that is the Development player (`engine.lib` 378 MB with debug information, about 107 MB without), not the Shipping one. A game links the stripped Development player as it links the full one; its own `.pdb` just has no frames of the engine until the symbols are unpacked, which replaces the libraries. The code is the same, so the build id matches (`setup_sdk.py` checks it). What a build needs stays in the SDK: `lib/gtest.pdb` and `lib/gtest_main.pdb` (the linker of a game's test reads them) and the import library. A plain `cmake --install` of a local build still installs the `.pdb` files and `source/` with the rest.

**Source package.** `<sdk>/source` (the engine's CMake input and `external/`, about 220 MB) is not in the SDK either, and is one package for every platform, made on Linux: a directory `wind-sdk-source-<version>/` with `source/` and `source.toml` (`version`). Only a build that compiles the engine needs it: Web, Android, a Debug SDK, or a standalone executable by hand (`add_subdirectory(<sdk>/source)`). The editor's Export links the player libraries and does not.

The `.sha256` is `sha256sum` format (`<hex>  <file name>`). `<os>` is the `platform` key of `sdk.toml` (`windows`, `linux`, `macos`; CMake's `Darwin` is written as `macos`). An archive holds one top directory (`wind-sdk-<version>-<os>-<arch>/`) with the contents of `out/sdk`, the same layout [CMake](CMake.md#editor-sdk) describes. The launcher's Install button downloads this archive, checks the `.sha256`, unpacks it, and checks the directory and `sdk.toml` before moving it to `sdk-<version>` in its install directory; it is the normal way to get an SDK on a developer machine, and `setup-wind` below is for CI. Unpacking it anywhere gives an SDK: the launcher's Locate takes that directory, and `%LOCALAPPDATA%\Programs\Wind\Sdks\<name>` is where it looks on its own.

## Getting an SDK in a game's CI

`tools/ci/setup_sdk.py` (Python 3.8+, standard library) and the composite action `.github/actions/setup-wind` around it:

- the version is `--version`, or the `engine` line of `<project>/wind_project.toml`;
- the platform and processor of the machine pick `<os>` and `<arch>`; the URL is `https://github.com/<repo>/releases/download/v<version>/<asset>` (`--repo`, `wismh/wind-engine` by default; the repository is public, a `GH_TOKEN` is used only to stay under the rate limit and never sent to the redirect target);
- the archive is checked against its `.sha256`, unpacked (a path that leaves the destination is refused), and the SDK is checked: `sdk.toml` has the asked `version` and this platform, and `bin/wind_editor` exists;
- run again with an SDK of that version in `--dest`, it downloads nothing;
- `--symbols` (the action's `symbols: true`) also unpacks the symbols package of this platform over the SDK, after checking that its `symbols.toml` has the same `version` and `build_id` as `sdk.toml`; `--source` (`source: true`) unpacks the source package to `<sdk>/source`, after checking `source.toml`. With the package already there (its `symbols.toml` or `source.toml` in `--dest` matches) nothing is downloaded;
- `--from-dir <directory>` takes the two files from a directory instead of the network (tests, mirrors).

The action caches `<runner temp>/wind-sdk/<version>` under `wind-sdk-<version>-<os>-<arch>` (with `-symbols` and `-source` when those are asked for), sets the output `sdk-dir` and the environment `WIND_SDK_DIR`, and adds `<sdk>/bin` to `PATH`. How a game uses it: the manual's [Continuous Integration](../../manual/getting-started/Continuous-Integration.md).

## Raising the minor or the stage

Edit the three lines at the top of the root `CMakeLists.txt` in the commit that should start the new line, and merge it:

```cmake
set(ENGINE_VERSION_MAJOR 0)
set(ENGINE_VERSION_MINOR 2)      # was 1: the merge releases 0.2.0a
set(ENGINE_VERSION_STAGE "b")    # was "a": the next release is a beta, its patch goes on
```

`next_version.py` reads exactly these lines (`set(ENGINE_VERSION_MAJOR n)`, `set(ENGINE_VERSION_STAGE "x")`); keep their form. Nothing else holds the version: the templates take `{{engine}}` from the SDK, the docs use `0.1.0` or `0.1.7a` as examples only.

## Limits

- **Verified on GitHub.** `Release` ran green on `main` (v0.1.0a, v0.1.1a), and `Setup Wind check` is green on all three systems against v0.1.1a. A change to the workflows is tested with the dry run above; the check workflow can only run against a real release.
- **Same toolchain as the SDK.** A game module and the SDK must agree on the build id (compiler, configuration, CRT, public headers), and `source/` and the Windows SDK are built with the runner's compiler. A game's CI must therefore build with a compatible compiler: Visual Studio 2026 (MSVC 19.5x) on Windows, the GCC of Ubuntu 24.04 on Linux, Apple Clang of Xcode on `macos-15`. A different compiler is refused at module load; the runner images are not pinned, so a runner update that changes the compiler version makes older SDKs unusable with it until the next release.
- **Player libraries** are built on all three systems; a Debug SDK has none. Only a Debug SDK, Web, and Android compile the engine, from the source package.
- **No Docker.** Not built or planned; the compiler match above is the reason one would want it.
- **One architecture per platform**: Windows x86_64, Linux x86_64, macOS arm64.
- A `-dev` SDK from a local build is not downloadable and not what a project's CI asks for.

## Tests

`tools/ci/test_next_version.py` (empty tags, a to b keeping the number, a new minor, junk tags, patches over 99, the `$GITHUB_OUTPUT` text) and `tools/ci/test_setup_sdk.py` (asset names and URLs of every platform, `engine` parsing, checksums, zip and tar.gz unpacking, refusing a wrong checksum, version, platform, or path, the symbols and source packages: unpacking over the SDK, idempotence, a wrong build or version, a missing package) and `tools/ci/test_split_symbols.py` (which libraries and binaries are split, the manifest) run with `python -m unittest discover -s tools/ci -p "test_*.py"`. `tests/engine_version_test.cpp` covers `EngineVersion`. Neither the workflow nor the action has a test run.

## See also

- [CMake](CMake.md#version), [Game Consumer](Game%20Consumer.md)
- [Project](../modules/Project.md), [Launcher](../features/Launcher.md)
