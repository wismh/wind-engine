# CMake Integration

The Wind engine provides the CMake helper `engine_add_game` to streamline target setup, C++23 flags, asset codegen, and packaging.

---

## The `engine_add_game` Command

In your `CMakeLists.txt`, after `find_package(Wind REQUIRED)` (the installed SDK, found through `CMAKE_PREFIX_PATH`):

```cmake
find_package(Wind REQUIRED)

engine_add_game(my_game
    src/main.cpp
    src/game.cpp
    src/systems/movement_system.cpp
)
```

### What `engine_add_game` Configures Automatically:
1. **Target Type:** Against the SDK, a game module (`my_game.dll`) the editor loads on Play. In a standalone build from the SDK's engine source, an executable on desktop/web or `libmain.so` on Android.
2. **C++ Standard:** Sets C++23 standard with compiler-specific optimizations and warning levels.
3. **Asset Codegen (`<game>_assets`):** Registers custom commands that run `asset_codegen` over the game's `assets/` folder, producing:
   - `<build>/generated/<game>/asset_ids.h`
   - `<build>/generated/<game>/catalog.toml`
4. **Include Directories:** Adds `<build>/generated/<game>` as a `PRIVATE` include path, so you can `#include <asset_ids.h>`.
5. **Icon Processing:** If `icon.png` exists next to the `CMakeLists.txt` that calls `engine_add_game`, runs `icon_codegen` to generate multi-resolution platform icons. The image must be square and at least 1024x1024, or the build fails.
6. **Post-Build Asset Staging:** Runs `engine_prepare_runtime`, copying assets and cooked catalogs adjacent to the output binary.

---

## Two Builds of One Project

The same `CMakeLists.txt` gives two different builds, picked by how CMake is configured:

| | Play (editor) | Export (standalone) |
| --- | --- | --- |
| Build directory | `build-editor/` | `build-export/` |
| Configure flag | none (`CMAKE_PREFIX_PATH=<sdk>`) | `-DWIND_EXPORT=ON` besides it |
| Configurations | `DebugGame;Release` (`Debug` against a Debug SDK) | `Release` |
| Engine | the SDK's shared `engine.dll`, imported | the SDK's engine source, built static |
| `engine_add_game` makes | a module (`my_game.dll`) | an executable |
| Recorded in | `build-editor/wind/<target>.<config>.module` | `build-export/wind/<target>.Release.export` |

The editor's Export button and `wind_editor --batch --export` run the second one for you ([Desktop](../platforms/Desktop.md#1-exporting-from-the-editor)). Keep your own presets for `build-editor`; do not point one at `build-export`, because its cache is made for the other mode.

---

## Configuration Options

These options apply to a standalone build, which adds the SDK's engine source instead of importing the SDK's engine: the editor's Export (`-DWIND_EXPORT=ON`), or `add_subdirectory(<sdk>/source wind)` for Web and Android. Against the SDK they are fixed by the SDK.

| Option | Default as a subdirectory | Description |
| --- | --- | --- |
| `ENGINE_WITH_WINDOW` | `ON` | Windowing, SDL3, and OpenGL / WebGL2 render backend. |
| `ENGINE_WITH_AUDIO` | `ON` | SDL_mixer audio system and SFX/Music playback. |
| `ENGINE_BUILD_TESTS` | `OFF` | Engine internal test suite (`engine_tests`). |
| `ENGINE_WITH_GTEST` | `OFF` | Vendors GoogleTest for the game's own test targets. |

### Disabling Audio (Headless or Audio-Free Titles)
If your game does not need audio, turn `ENGINE_WITH_AUDIO` off **before** the engine source is added. For an export build, pass it on the command line:

```bash
cmake -S . -B build-export -DWIND_EXPORT=ON -DCMAKE_PREFIX_PATH=<sdk> -DWind_DIR=<sdk>/cmake \
      -DCMAKE_CONFIGURATION_TYPES=Release -DENGINE_WITH_AUDIO=OFF
```

For a `CMakeLists.txt` that calls `add_subdirectory` itself (Web and Android), set the cache entry first:

```cmake
set(ENGINE_WITH_AUDIO OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/sdk/source wind)
```

> [!NOTE]
> A cache entry that exists before the engine's `option()` call is kept, so the value has to be set before the engine source is added. The editor's Play build imports the SDK's engine and ignores this option.

---

## Adding Game Unit Tests

GoogleTest ships in the SDK, built with its runtime, so `find_package(Wind)` already gives `GTest::gtest_main` and `gtest_discover_tests`:

```cmake
find_package(Wind REQUIRED)
enable_testing()

# Your test executable
add_executable(game_tests
    tests/domain_test.cpp
    src/domain/inventory.cpp
)
target_link_libraries(game_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(game_tests DISCOVERY_MODE PRE_TEST)
```

Run them with `ctest --test-dir build-editor -C DebugGame`.

This keeps your unit test suite fast, deterministic, and free from engine dependencies.

---

## Recommended Presets

`CMakePresets.json` (committed). The configurations have to be set before `project()`: against a Release SDK only `DebugGame` and `Release` are allowed.

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "editor",
      "displayName": "Game module for the Wind editor",
      "generator": "Visual Studio 18 2026",
      "binaryDir": "${sourceDir}/build-editor",
      "cacheVariables": { "CMAKE_CONFIGURATION_TYPES": "DebugGame;Release" }
    }
  ]
}
```

`CMakeUserPresets.json` (not committed) points at the SDK on this machine:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "editor-local",
      "inherits": "editor",
      "cacheVariables": { "CMAKE_PREFIX_PATH": "C:/path/to/wind-engine/out/sdk" }
    }
  ]
}
```

Then build from the command line:

```bash
cmake --preset editor-local
cmake --build build-editor --config DebugGame
```

---

## Next Steps

- Explore the [Game Lifecycle](../architecture/Game-Lifecycle.md) to start structuring your game code.
- Check [Target Platforms](../platforms/Desktop.md) for Web and Android build pipelines.
