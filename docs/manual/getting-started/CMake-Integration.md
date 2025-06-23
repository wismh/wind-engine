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
5. **Icon Processing:** If `icon.png` exists in the game root, runs `icon_codegen` to generate multi-resolution platform icons.
6. **Post-Build Asset Staging:** Runs `engine_prepare_runtime`, copying assets and cooked catalogs adjacent to the output binary.

---

## Configuration Options

These options apply to a standalone build, which adds the SDK's engine source with `add_subdirectory(<sdk>/source wind)` instead of `find_package` (until the editor exports games). Against the SDK they are fixed by the SDK.

| Option | Default as a subdirectory | Description |
| --- | --- | --- |
| `ENGINE_WITH_WINDOW` | `ON` | Windowing, SDL3, and OpenGL / WebGL2 render backend. |
| `ENGINE_WITH_AUDIO` | `ON` | SDL_mixer audio system and SFX/Music playback. |
| `ENGINE_BUILD_TESTS` | `OFF` | Engine internal test suite (`engine_tests`). |
| `ENGINE_WITH_GTEST` | `OFF` | Vendors GoogleTest for the game's own test targets. |

### Disabling Audio (Headless or Audio-Free Titles)
If your game does not need audio, override `ENGINE_WITH_AUDIO` **before** `add_subdirectory`:

```cmake
set(ENGINE_WITH_AUDIO OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/sdk/source wind)
```

> [!NOTE]
> Setting the cache variable with `FORCE` is required before `add_subdirectory` because CMake's `option()` command does not overwrite existing cache variables.

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
