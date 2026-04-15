# Game consumer

A game keeps Wind as a git submodule, usually `external/engine`, and does not edit that checkout to add features. Change the engine repo, then move the pin.

```cmake
add_subdirectory(external/engine)
engine_add_game(my_game
    src/main.cpp
    src/game.cpp)
```

At this point `ENGINE_WITH_WINDOW` and `ENGINE_WITH_AUDIO` default ON, and `ENGINE_BUILD_TESTS` defaults OFF. To drop the mixer:

```cmake
set(ENGINE_WITH_AUDIO OFF CACHE BOOL "" FORCE)
add_subdirectory(external/engine)
```

The `FORCE` has to be set before `add_subdirectory`. `option()` will not replace a cache entry that already exists.

`ENGINE_WITH_GTEST=ON` vendors GoogleTest for the game's own tests without compiling `engine_tests`.

What `engine_add_game` adds (executable, Android `libmain`, or the editor's game module; C++23, asset cook, optional `icon.png`, runtime copy) is [CMake](CMake.md) and [Pipeline](Pipeline.md).

## Editor module

The editor is not built in the game's tree. Build and install the editor SDK once from the engine repo:

```bash
cmake --preset vs-editor
cmake --build build-editor --config Release
cmake --install build-editor --config Release --prefix out/sdk
```

Then configure the game with the cache path `WIND_EDITOR_SDK` pointing at that SDK. The game's `CMakeLists.txt` does not change. `add_subdirectory(external/engine)` imports `engine`, `glm::glm`, `asset_codegen`, and `icon_codegen` from the SDK and compiles no engine, and `engine_add_game` builds a module (`bin/<config>/<game>.dll` with `assets/catalog.toml` beside it). `ENGINE_WITH_GTEST` still adds GoogleTest from the submodule. See [CMake](CMake.md#sdk-mode).

The game builds in two configurations against the Release SDK: `DebugGame` (game code unoptimized with symbols and asserts, `/MD`) and `Release`. Both link the same Release `engine.dll`; Debug, RelWithDebInfo, and MinSizeRel are a configure error in SDK mode ([CMake](CMake.md#configurations)). The list has to be set before the game's `project()`, so the committed `editor` preset sets it with the generator and binary directory:

```json
{
  "name": "editor",
  "generator": "Visual Studio 18 2026",
  "binaryDir": "${sourceDir}/build-editor",
  "cacheVariables": {
    "CMAKE_CONFIGURATION_TYPES": "DebugGame;Release"
  }
}
```

The SDK path is per machine, so it goes in the game's `CMakeUserPresets.json` (not committed), on top of that preset:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "editor-local",
      "inherits": "editor",
      "cacheVariables": {
        "WIND_EDITOR_SDK": "C:/path/to/engine/out/sdk"
      }
    }
  ]
}
```

```bash
cmake --preset editor-local
cmake --build build-editor --config DebugGame --target my_game
C:/path/to/engine/out/sdk/bin/wind_editor.exe --game build-editor/bin/DebugGame/my_game.dll --play
```

`--config Release` builds the optimized module into `bin/Release/`. A build directory configured earlier with CMake's default configurations fails with a message: delete it and configure again. `ENGINE_EDITOR=ON` in a game tree without `WIND_EDITOR_SDK` is a configure error.

A module built with the debug CRT (`/MDd` or `_DEBUG`) does not compile: `<engine/game_entry.h>` stops it with "Build it in DebugGame or Release against a Release SDK" ([CRT guard](CMake.md#crt-guard)). To step through engine code as well, install a Debug SDK (`--config Debug --prefix out/sdk-debug`) and point a second user preset at it with `"CMAKE_CONFIGURATION_TYPES": "Debug"`.

Game code may test `ENGINE_EDITOR` only for tools (an editor-only overlay or cheat panel). Gameplay must not depend on it, or the game in the editor behaves unlike the exported game ([Principles](../architecture/Principles.md)).

### Debugging game code

The module and the SDK both carry `.pdb` files, in `DebugGame` and in `Release`. The editor loads a copy of the module from `live/<n>/` with its `.pdb` beside it, and the module records only the `.pdb` file name (`/PDBALTPATH`), so the debugger finds that copy.

- Start under the debugger: open `build-editor/<game>.sln`, set the game target as the startup project, and in its Debugging properties set Command to `C:/path/to/engine/out/sdk/bin/wind_editor.exe` and Command Arguments to `--game $(TargetPath) --play`. Build `DebugGame` and press F5. Breakpoints in game code hit once Play loads the module.
- Attach: start `wind_editor.exe` yourself, then Debug > Attach to Process > `wind_editor.exe`. Breakpoints bind when the module loads on Play.

## `main`

```cpp
#include <engine/game_entry.h>

#include <game/game.h>

ENGINE_GAME(game::Game)
```

`ENGINE_GAME` expands to `main` running `Engine<game::Game>`. In a module build (SDK mode) it expands to the module exports instead and checks the CRT. See [Core](../modules/Core.md).

`Game` is constructed from `const engine::EngineServices&`. `ENGINE_GAME` and `Engine<GameT>` do not compile without that constructor. `GameBase` is enough for a test that never calls `Engine::run`.

The game includes `<engine/…>` only. It does not add `engine/src` to its include path and does not include SDL, glad, or NanoVG.

## Android identity

`cmake/android/app/` is the engine template (manifest, `strings.xml`, `res/`). Two games that ship it unchanged share `applicationId`, the display name, and the launcher icon. `build.gradle` reads these properties (a `-P` property or the same-named environment variable):

| Property | Effect | Default |
| --- | --- | --- |
| `ENGINE_ANDROID_APPLICATION_ID` | `defaultConfig.applicationId` | `org.windengine.app` |
| `ENGINE_ANDROID_APP_NAME` | manifest placeholder `appName` (`android:label="${appName}"`) | `Wind` |
| `ENGINE_ANDROID_RES_DIR` | added to `sourceSets.debug.res.srcDirs` and `sourceSets.release.res.srcDirs` | empty |
| `ENGINE_ANDROID_MANIFEST` | game manifest overlay (`sourceSets.{debug,release}.manifest.srcFile`) | empty |
| `ENGINE_ANDROID_ASSETS_OUT` | `assets.srcDirs` and the CMake stage directory | `${buildDir}/wind-assets` |
| `ENGINE_HOST_ASSET_CODEGEN` | passed through to the native CMake arguments | required when cross-compiling |
| `ENGINE_HOST_ICON_CODEGEN` | same | required when cross-compiling |

The app name is a manifest placeholder, not a second `values/strings.xml`. AGP only lets a build-variant source set override `main`. Two directories on `main.res.srcDirs` are siblings, and AAPT2 fails the build when both declare `string/app_name`.

The engine template ships `mipmap-*/ic_launcher.png` under `cmake/android/app/src/main/res/` because the manifest always references `@mipmap/ic_launcher`. A game overlay goes on the debug and release source sets, not on `main`, for the same duplicate-resource reason.

`icon_codegen` writes `mipmap-*/ic_launcher.png` into `ENGINE_GAME_ICON_DIR` for a game that has `icon.png`. Gradle resolves `res.srcDirs` at configuration time, before that custom command runs, so nothing copies those PNGs into `ENGINE_ANDROID_RES_DIR` automatically. The game points `ENGINE_ANDROID_RES_DIR` at a directory it fills itself.

A game supplies its own manifest additions (extra `<uses-permission>`, `<queries>`, services, or `tools:node="remove"`) by putting `android/AndroidManifest.xml` (or `AndroidManifest.xml`) next to its `CMakeLists.txt` (detected by `engine_configure_app` as `ENGINE_GAME_ANDROID_MANIFEST`) or by setting `ENGINE_ANDROID_MANIFEST`. AGP's standard Manifest Merger merges that overlay over the engine template.

ABI in the template is `arm64-v8a`. `minSdk` is 21. `compileSdk` and `targetSdk` are 35.

## Web

`engine_add_web_game` is `engine_add_game` after an Emscripten configure. Cook assets with a native `asset_codegen` first and pass `ENGINE_HOST_ASSET_CODEGEN`. The shell is `cmake/web/shell.html` unless `ENGINE_WEB_SHELL` is set. Preload paths are [Runtime Assets](Runtime%20Assets.md).

## See also

- [Icon Codegen](Icon%20Codegen.md)
- [Principles](../architecture/Principles.md)
