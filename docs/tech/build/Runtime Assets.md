# Runtime assets

`EngineRuntime::assets_root()` is `runtime_assets_root(base_path())`. `base_path()` is the SDL base path (`SDL_GetBasePath` in the windowed presentation), not the process working directory.

## Desktop

`default_assets_root` is `<base>/assets`. An empty base yields an empty root, and `Engine::init` reports "Assets root is missing".

`engine_prepare_runtime` leaves this tree beside the executable:

```
your-game.exe
assets/
  catalog.toml
  textures/…
  engine/
    catalog.toml
    shaders/unlit.shader
    meshes/quad.mesh
    materials/unlit.mat
    fonts/ui.ttf
    fonts/math.otf
    textures/splash.png
```

The `engine/` directory is a copy of `builtin_assets/` with the cooked `catalog.toml` copied over it. The game catalog is the cooked file, copied only when the target has `ENGINE_GAME_CATALOG`. A game with no `assets/` directory skips game codegen. Only the engine tree is staged.

`init` loads:

1. `assets/engine/catalog.toml`, file root `assets/engine`. Failure is fatal.
2. `assets/catalog.toml`, file root `assets`. `MetaError::Io` (the file is absent) is ignored. Any other error is fatal.

Builtin GUIDs are compiled into `include/engine/builtin_ids.h`. The cooked engine catalog must keep those same GUIDs. Do not regenerate them.

Game code includes the generated `asset_ids.h` from the build tree. It does not open files by name.

## Web

`Platform::Web` ignores the base path and uses `/assets` (`packaged_assets_mount`).

`engine_target_web_preload` (`cmake/web/link_flags.cmake`) maps:

| Source | Virtual path |
| --- | --- |
| `builtin_assets/` | `/assets/engine` |
| cooked engine catalog | `/assets/engine/catalog.toml` |
| game `assets/` | `/assets` |
| cooked game catalog | `/assets/catalog.toml` |

`user_data_directory` is a different tree, under `/storage`. See [Core](../modules/Core.md).

## Android

`runtime_assets_root` stages a tree `AssetsDb` can `ifstream`. SDL's `assets://` prefix is not a `std::filesystem` path (`apk_assets_mount` returns that string separately).

`android_runtime_assets_root` (`src/core/platform.cpp`) returns `<internal storage>/assets`.

If `assets/engine/catalog.toml` is already in that tree, it returns the tree and does not copy again.

Otherwise it copies `base/assets` only when all of these hold:

- `base` is non-empty
- `base.generic_string()` is not `.` or `./`
- that string does not contain `assets:`
- `base/assets` exists and is a directory

If any condition fails, it copies both `catalog.toml` and `engine/catalog.toml` through SDL I/O, then stages every relative path in each cooked catalog. Engine entries are read with the `engine/` prefix and written under `assets/engine`.

Game writes go to `<internal storage>/user/`, not into `assets/`.

Gradle packs the same staged tree when `ENGINE_ANDROID_ASSETS_OUT` is set (`engine_prepare_runtime` copies `<exe>/assets` there). The engine's `cmake/android/app/build.gradle` points `assets.srcDirs` at that directory, defaulting to `${project.buildDir}/wind-assets`.

## See also

- [Pipeline](Pipeline.md)
- [Assets](../features/Assets.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
