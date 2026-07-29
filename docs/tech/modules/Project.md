# Project

The two small files that tie a game repo to an editor SDK, and their readers. The editor reads both when it opens a project; the launcher (its own repository, `wismh/wind-launcher`) reads them to list projects and SDKs. In the engine library, not the editor, so both tools share one reader.

## Files

`wind_project.toml`, in a game repo's root. It makes the directory a Wind project:

```toml
name = "Tic Tac Toe"    # optional; the directory name when absent
engine = "0.1.7a"       # the engine version the project builds against, the SDK's version as exact text
target = "tic-tac-toe"  # the engine_add_game target the editor builds and plays
```

`sdk.toml`, at an installed SDK's root, written by `cmake/sdk_manifest.cmake` on `cmake --install` ([CMake](../build/CMake.md#editor-sdk)): `version` (the full version, `0.1.7a` or `0.1.0a-dev`), `commit`, `dirty`, `config`, `build_id`, all required.

Keys a reader does not know are ignored, so an older reader reads a newer file; a key is never renamed.

## API

`include/engine/project/wind_project.h`, `sdk_manifest.h`, `manifest_error.h`, `engine_version.h`.

| Call | Returns |
| --- | --- |
| `read_wind_project(dir)` | `WindProject` (`name`, `engine`, `target`) from `<dir>/wind_project.toml` (`kWindProjectFile`) |
| `read_sdk_manifest(sdk_root)` | `SdkManifest` (`version`, `commit`, `dirty`, `config`, `build_id`) from `<sdk_root>/sdk.toml` (`kSdkManifestFile`) |
| `describe(failure)` | One status-bar line: "Missing key: wind_project.toml: target" |
| `parse_engine_version(text)` | `std::optional<EngineVersion>`; nullopt unless the text is a version ([Engine version](#engine-version)) |
| `to_string(version)` | The text `parse_engine_version` reads back |

Errors are `ManifestFailure{kind, detail}`:

| `ManifestError` | When | `detail` |
| --- | --- | --- |
| `Missing` | The file does not exist or cannot be read | the path |
| `Malformed` | Not valid TOML | path, line, and the parser's message |
| `MissingKey` | A required key is absent | file name and key |
| `WrongType` | A key holds another TOML type | file name, key, and the type it needs |

The readers parse with toml++ (`src/project/manifest_table.cpp`), which stays private to the engine.

## Versions

The project's `engine` must equal the SDK's `version` exactly, as text: `0.1.7a`, `0.1.7b`, and `0.1.7` are three engines. The editor refuses another one, and `find_package(Wind)` accepts only its own three numbers ([CMake](../build/CMake.md#version)). The build id stays the last check when a module loads.

### Engine version

`<major>.<minor>.<patch><stage>` with an optional `-dev`, `^\d+\.\d+\.\d+[ab]?(-dev)?$`: `0.1.7a` (alpha), `0.2.6b` (beta), `1.0.0` (production), `0.2.7a-dev` (a local build of the engine, [Release](../build/Release.md)). The patch has no limit (`0.1.100a`).

```cpp
struct EngineVersion { int major, minor, patch; char stage; bool dev; };   // stage: 'a', 'b', or 0 for production
```

`parse_engine_version` accepts only that shape (no sign, space, fourth number, other letter, or a number that does not fit an `int`). `EngineVersion` orders by `operator<=>`: `major.minor.patch` numerically; for one number a `-dev` build is older than every release of it, then `a` < `b` < production: `0.1.99a < 0.1.100a < 0.2.0a-dev < 0.2.0a < 0.2.0b < 0.2.0`. Only ordering uses it (the launcher's SDK list); an SDK is matched to a project by the text.

## Tests

`tests/project_test.cpp`: both files read, the name default (also for a directory with a trailing separator), unknown keys ignored, each error kind with its detail, and `sdk.toml` versions with a stage and `-dev` read as written. `tests/engine_version_test.cpp`: every shape parsed, what is refused, `to_string` round trip, and the order (patch past 99, stage, `-dev`).

## See also

- [Editor](../features/Editor.md)
- [Editor Plan](../architecture/Editor%20Plan.md)
