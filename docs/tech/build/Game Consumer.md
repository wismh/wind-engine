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

What `engine_add_game` adds (executable or Android `libmain`, C++23, asset cook, optional `icon.png`, runtime copy) is [CMake](CMake.md) and [Pipeline](Pipeline.md).

## `main`

```cpp
#include <engine/engine.h>

#include <game/game.h>

int main() {
    engine::Engine<game::Game> app;
    if (!app.init()) {
        return 1;
    }
    return app.run();
}
```

`Game` is constructed from `const engine::EngineServices&`. `Engine<GameT>` does not compile without that constructor. `GameBase` is enough for a test that never calls `Engine::run`.

The game includes `<engine/…>` only. It does not add `engine/src` to its include path and does not include SDL, glad, or NanoVG.

## Android identity

`cmake/android/app/` is the engine template (manifest, `strings.xml`, `res/`). Two games that ship it unchanged share `applicationId`, the display name, and the launcher icon. `build.gradle` reads these properties (a `-P` property or the same-named environment variable):

| Property | Effect | Default |
| --- | --- | --- |
| `ENGINE_ANDROID_APPLICATION_ID` | `defaultConfig.applicationId` | `org.windengine.app` |
| `ENGINE_ANDROID_APP_NAME` | manifest placeholder `appName` (`android:label="${appName}"`) | `Wind` |
| `ENGINE_ANDROID_RES_DIR` | added to `sourceSets.debug.res.srcDirs` and `sourceSets.release.res.srcDirs` | empty |
| `ENGINE_ANDROID_ASSETS_OUT` | `assets.srcDirs` and the CMake stage directory | `${buildDir}/wind-assets` |
| `ENGINE_HOST_ASSET_CODEGEN` | passed through to the native CMake arguments | required when cross-compiling |
| `ENGINE_HOST_ICON_CODEGEN` | same | required when cross-compiling |

The app name is a manifest placeholder, not a second `values/strings.xml`. AGP only lets a build-variant source set override `main`. Two directories on `main.res.srcDirs` are siblings, and AAPT2 fails the build when both declare `string/app_name`.

The engine template ships `mipmap-*/ic_launcher.png` under `cmake/android/app/src/main/res/` because the manifest always references `@mipmap/ic_launcher`. A game overlay goes on the debug and release source sets, not on `main`, for the same duplicate-resource reason.

`icon_codegen` writes `mipmap-*/ic_launcher.png` into `ENGINE_GAME_ICON_DIR` for a game that has `icon.png`. Gradle resolves `res.srcDirs` at configuration time, before that custom command runs, so nothing copies those PNGs into `ENGINE_ANDROID_RES_DIR` automatically. The game points `ENGINE_ANDROID_RES_DIR` at a directory it fills itself.

ABI in the template is `arm64-v8a`. `minSdk` is 21. `compileSdk` and `targetSdk` are 35.

## Web

`engine_add_web_game` is `engine_add_game` after an Emscripten configure. Cook assets with a native `asset_codegen` first and pass `ENGINE_HOST_ASSET_CODEGEN`. The shell is `cmake/web/shell.html` unless `ENGINE_WEB_SHELL` is set. Preload paths are [Runtime Assets](Runtime Assets.md).

## See also

- [Icon Codegen](Icon Codegen.md)
- [Principles](../architecture/Principles.md)
