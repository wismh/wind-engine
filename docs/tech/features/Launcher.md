# Launcher

The Wind Launcher lists your projects and the editor SDKs installed on the machine, makes a new project from an SDK's template, and opens a project in the editor of the engine version it names.

**It no longer lives in this repository.** Since wind-207 it is an ordinary Wind game project of its own, [`wismh/wind-laucnher`](https://github.com/wismh/wind-laucnher): `wind_project.toml`, `find_package(Wind)`, `engine_add_game`, built against a released SDK like any game. It has its own CI, its own version (`version.txt`, independent of the engine's), and its own releases:

- [Latest launcher](https://github.com/wismh/wind-laucnher/releases/latest) with stable download names: `wind-launcher-windows-x86_64.zip`, `wind-launcher-linux-x86_64.tar.gz`, `wind-launcher-macos-arm64.tar.gz`.
- [All versions](https://github.com/wismh/wind-laucnher/releases), `v<major>.<minor>.<patch><stage>`.

Its README says how it is built and where it keeps things.

## What the engine keeps for it

The launcher includes only `<engine/...>` headers and reads what the SDK gives it:

- `sdk.toml` of every SDK ([CMake](../build/CMake.md#editor-sdk)), read with `engine::read_sdk_manifest`; `wind_project.toml` with `engine::read_wind_project` ([Project](../modules/Project.md)).
- `engine::EngineVersion` for the order of SDK versions ([Release](../build/Release.md#version)).
- The SDK's `templates/` (`templates/empty` in this repository), which a new project is copied from, with the placeholders `{{name}}`, `{{engine}}`, `{{target}}`, and `{{sdk}}`. A change to a template or its placeholders is a change to the launcher's contract: the launcher's tests make a project from the template of the SDK they are built against.
- `IProcessLauncher` ([Process](../modules/Process.md)) to start an editor, a file manager, `curl`, and `tar`.
- The release assets: `wind-sdk-<version>-<os>-<arch>.<zip|tar.gz>` with a `.sha256` ([Release](../build/Release.md#assets)).

## Template

`templates/empty/` is the project a new project starts from: `wind_project.toml`, `CMakeLists.txt`, `CMakePresets.json`, `CMakeUserPresets.json` (with `{{sdk}}`), `src/`, `assets/`. The SDK installs it as `<sdk>/templates/empty`. `{{name}}` is the display name, `{{target}}` the CMake target made from it, `{{engine}}` the SDK's version, and `{{sdk}}` the SDK root.
