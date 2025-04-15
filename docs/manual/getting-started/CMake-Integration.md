# CMake Integration

The Wind engine provides the CMake helper `engine_add_game` to streamline target setup, C++23 flags, asset codegen, and packaging.

---

## The `engine_add_game` Command

In your `CMakeLists.txt`:

```cmake
add_subdirectory(external/engine)

engine_add_game(my_game
    src/main.cpp
    src/game.cpp
    src/systems/movement_system.cpp
)
```

### What `engine_add_game` Configures Automatically:
1. **Target Type:** Creates an executable on desktop/web, or a shared library `libmain.so` on Android.
2. **C++ Standard:** Sets C++23 standard with compiler-specific optimizations and warning levels.
3. **Asset Codegen (`<game>_assets`):** Registers custom commands that run `asset_codegen` over the game's `assets/` folder, producing:
   - `<build>/generated/<game>/asset_ids.h`
   - `<build>/generated/<game>/catalog.toml`
4. **Include Directories:** Adds `<build>/generated/<game>` as a `PRIVATE` include path, so you can `#include <asset_ids.h>`.
5. **Icon Processing:** If `icon.png` exists in the game root, runs `icon_codegen` to generate multi-resolution platform icons.
6. **Post-Build Asset Staging:** Runs `engine_prepare_runtime`, copying assets and cooked catalogs adjacent to the output binary.

---

## Configuration Options

When configuring Wind as a submodule, several CMake options control features:

| Option | Default in Submodule | Description |
| --- | --- | --- |
| `ENGINE_WITH_WINDOW` | `ON` | Windowing, SDL3, and OpenGL / WebGL2 render backend. |
| `ENGINE_WITH_AUDIO` | `ON` | SDL_mixer audio system and SFX/Music playback. |
| `ENGINE_BUILD_TESTS` | `OFF` | Engine internal test suite (`engine_tests`). |
| `ENGINE_WITH_GTEST` | `OFF` | Vendors GoogleTest for the game's own test targets. |

### Disabling Audio (Headless or Audio-Free Titles)
If your game does not need audio, override `ENGINE_WITH_AUDIO` **before** `add_subdirectory`:

```cmake
set(ENGINE_WITH_AUDIO OFF CACHE BOOL "" FORCE)
add_subdirectory(external/engine)
```

> [!NOTE]
> Setting the cache variable with `FORCE` is required before `add_subdirectory` because CMake's `option()` command does not overwrite existing cache variables.

---

## Adding Game Unit Tests

To write unit tests for your gameplay domain using GoogleTest:

```cmake
# Enable GoogleTest without compiling engine_tests
set(ENGINE_WITH_GTEST ON CACHE BOOL "" FORCE)
add_subdirectory(external/engine)

# Your test executable
add_executable(game_tests
    tests/domain_test.cpp
    src/domain/inventory.cpp
)
target_link_libraries(game_tests PRIVATE GTest::gtest_main)
```

This keeps your unit test suite fast, deterministic, and free from engine dependencies.

---

## Recommended `CMakePresets.json`

Create a `CMakePresets.json` in your repository root:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "dev",
      "displayName": "Developer Build",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build"
    },
    {
      "name": "ninja-dev",
      "displayName": "Ninja Clang/GCC",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "dev",
      "configurePreset": "dev",
      "configuration": "RelWithDebInfo"
    }
  ]
}
```

Then build from the command line:

```bash
cmake --preset dev
cmake --build --preset dev
```

---

## Next Steps

- Explore the [Game Lifecycle](../architecture/Game-Lifecycle.md) to start structuring your game code.
- Check [Target Platforms](../platforms/Desktop.md) for Web and Android build pipelines.
