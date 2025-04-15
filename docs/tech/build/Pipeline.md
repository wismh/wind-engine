# Pipeline

From a clean tree to a running game. The root [README](../../../README.md) is the command list. This page is the order CMake actually runs.

## 1. Configure

```bash
git submodule update --init --recursive
cmake --preset vs
```

`vs` is the engine-root preset: tests ON, window OFF, audio OFF. A game repo uses `add_subdirectory` instead of this preset. Defaults flip. See [CMake](CMake.md).

Configure vendors glm, tomlplusplus, tinyxml2, and spdlog. Window and audio submodules are required only when those options are ON. Cross compiles (`EMSCRIPTEN`, Android) refuse to configure unless `ENGINE_HOST_ASSET_CODEGEN` and `ENGINE_HOST_ICON_CODEGEN` point at native binaries built by a previous host configure.

## 2. Codegen

`asset_codegen` is a host executable that links `engine`.

| Target | Input | Output |
| --- | --- | --- |
| `engine_builtin_catalog` | `builtin_assets/` | `<build>/generated/engine/catalog.toml` (`--engine`, empty reserved set) |
| `<game>_assets` | the game's `assets/` | `<build>/generated/<game>/asset_ids.h` and `catalog.toml` |

Both are custom commands with `DEPENDS` on the tool and a `GLOB_RECURSE` of the asset tree (`CONFIGURE_DEPENDS`, so a new file retriggers configure). A missing `.meta`, a bad GUID, a GUID collision, a bad `.strings` table, or a `{tr}` key missing from the source table fails the build. The tool never writes `.meta`.

`asset_guid` is a separate executable. Nothing in the default build runs it.

If the game has `icon.png` at the directory that called `engine_add_game`, `icon_codegen` writes `generated/<game>/icons/`. See [Icon Codegen](Icon%20Codegen.md).

## 3. Compile

`engine` is a static library. Sources are every `src/**/*.cpp` except `src/render/opengl/**`, `engine_runtime.cpp`, `engine_instantiate.cpp`, and `sdl_fatal_error.cpp`. Those four are added only when `ENGINE_WITH_WINDOW` is ON.

`engine_add_game` adds the game executable (or, on Android, a shared library named `main`) and makes it depend on the codegen targets. The generated directory is a PRIVATE include.

## 4. Copy assets

`engine_prepare_runtime` is a POST_BUILD on the game and on `engine_tests`:

1. If the caller has an `assets/` directory, copy it beside the binary.
2. Copy `builtin_assets/` to `<exe>/assets/engine/`.
3. Copy the cooked engine `catalog.toml` over that tree's catalog.
4. If the target has `ENGINE_GAME_CATALOG`, copy it to `<exe>/assets/catalog.toml`.
5. On Android, if `ENGINE_ANDROID_ASSETS_OUT` is set, copy that `assets/` tree there for Gradle.

Web does not rely on that copy for the page load. `engine_target_web_preload` adds `--preload-file` mappings onto `/assets` and `/assets/engine`, including the cooked catalogs. See [Runtime Assets](Runtime%20Assets.md).

## 5. Run

A windowed `main` constructs `Engine<Game>`, calls `init`, then `run`. `init` loads `assets/engine/catalog.toml` and then `assets/catalog.toml`. The frame order is [Runtime Loop](../architecture/Runtime%20Loop.md).

`engine_tests` is the headless binary. `ctest` discovers it with `gtest_discover_tests` on desktop. Emscripten links it with the web flags. Android configures the suite and does not treat a device run as a merge gate.

## See also

- [Asset Codegen](Asset%20Codegen.md)
- [Game Consumer](Game%20Consumer.md)
