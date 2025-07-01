# Project

The two small files that tie a game repo to an editor SDK, and their readers. The editor reads both when it opens a project; the launcher (wind-179) will read them to list projects and SDKs. In the engine library, not the editor, so both tools share one reader.

## Files

`wind_project.toml`, in a game repo's root. It makes the directory a Wind project:

```toml
name = "Tic Tac Toe"    # optional; the directory name when absent
engine = "0.1.0"        # the engine version the project builds against
target = "tic-tac-toe"  # the engine_add_game target the editor builds and plays
```

`sdk.toml`, at an installed SDK's root, written by `cmake/sdk_manifest.cmake` on `cmake --install` ([CMake](../build/CMake.md#editor-sdk)): `version`, `commit`, `dirty`, `config`, `build_id`, all required.

Keys a reader does not know are ignored, so an older reader reads a newer file; a key is never renamed.

## API

`include/engine/project/wind_project.h`, `sdk_manifest.h`, `manifest_error.h`.

| Call | Returns |
| --- | --- |
| `read_wind_project(dir)` | `WindProject` (`name`, `engine`, `target`) from `<dir>/wind_project.toml` (`kWindProjectFile`) |
| `read_sdk_manifest(sdk_root)` | `SdkManifest` (`version`, `commit`, `dirty`, `config`, `build_id`) from `<sdk_root>/sdk.toml` (`kSdkManifestFile`) |
| `describe(failure)` | One status-bar line: "Missing key: wind_project.toml: target" |

Errors are `ManifestFailure{kind, detail}`:

| `ManifestError` | When | `detail` |
| --- | --- | --- |
| `Missing` | The file does not exist or cannot be read | the path |
| `Malformed` | Not valid TOML | path, line, and the parser's message |
| `MissingKey` | A required key is absent | file name and key |
| `WrongType` | A key holds another TOML type | file name, key, and the type it needs |

The readers parse with toml++ (`src/project/manifest_table.cpp`), which stays private to the engine.

## Versions

The project's `engine` must equal the SDK's `version` exactly: the editor refuses another one, and `find_package(Wind)` accepts only its own version ([CMake](../build/CMake.md#version)). The build id stays the last check when a module loads.

## Tests

`tests/project_test.cpp`: both files read, the name default (also for a directory with a trailing separator), unknown keys ignored, and each error kind with its detail.

## See also

- [Editor](../features/Editor.md)
- [Editor Plan](../architecture/Editor%20Plan.md)
