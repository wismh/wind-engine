---
tags: [build]
---

# Consuming Wind from a game

Wind is a **git submodule** (e.g. `external/engine`, url `../engine`).

```cmake
set(ENGINE_WITH_AUDIO ON CACHE BOOL "" FORCE)  # optional
add_subdirectory(external/engine)
engine_add_game(my_game
    src/main.cpp
    src/game.cpp)
```

`engine_add_game`:

- `add_executable` + C++23 + MSVC warnings
- `PRIVATE engine`
- `include/` of the game if present
- asset codegen + `target_include_directories` generated dir
- packaging icon generation (`icon.png` → `.ico`/`.icns`/mipmaps/favicon) when present; on Windows, `icon.rc` embeds `icon.ico` into the target
- `engine_prepare_runtime`

`main.cpp` typically:

```cpp
#include <engine/engine.h>
#include <game/game.h>
int main() {
    engine::Engine<game::Game> app;
    if (!app.init()) return 1;
    return app.run();
}
```

`Game` constructor is Boost.DI-injected (`AssetsDb&`, `InputSystem&`, `IAudioSystem&`, …). Prefer `GameBase`.

Pin the submodule to a Wind `main` commit; do not develop features inside the nested copy — edit the engine repo directly, then pin.

## Android per-game identity overlay

`cmake/android/app/` (manifest, `strings.xml`, `res/`) is an engine-owned template shared by every
game that builds for Android — without an overlay, two games would collide on the same
`app_name`, `applicationId`, and launcher icon. `cmake/android/app/build.gradle` resolves more
`-P` properties, mirroring `ENGINE_ANDROID_ASSETS_OUT` — but **not all three identity pieces go
through the same mechanism**; an earlier version of this doc claimed a single `res.srcDirs`
overlay handled all of them, which a downstream game's real Gradle build proved wrong (AAPT2
"Duplicate resources" on `strings.xml`):

- `ENGINE_ANDROID_APPLICATION_ID` — overrides `defaultConfig.applicationId`; defaults to
  `org.windengine.app` when absent. Plain Gradle config.
- `ENGINE_ANDROID_APP_NAME` — feeds `defaultConfig.manifestPlaceholders = [appName: gameAppName ?:
  'Wind']`; the manifest's `<application>`/`<activity>` use `android:label="${appName}"`. **Not**
  a `values/strings.xml` overlay: AGP only gives real override precedence to build-variant source
  sets over `main` (a flavor or build type over it), not to multiple directories added to `main`'s
  own `res.srcDirs` list — those are siblings, and AAPT2 hard-fails the build the moment two of
  them declare the same `string/app_name`. `manifestPlaceholders` is manifest-merger territory,
  not resource-merger territory, so it sidesteps the collision entirely.
- `ENGINE_ANDROID_RES_DIR` — a directory the game supplies (`mipmap-*/ic_launcher.png`). Added to
  `sourceSets.main.res.srcDirs`, and this one genuinely does work as an overlay — but only because
  the engine's own `res/` declares zero `mipmap-*` resources, so there is nothing for a game's
  entry to collide with; it's the sole definition, not an override. The committed engine template
  is never edited per game.
- `ENGINE_HOST_ICON_CODEGEN` — threaded into `externalNativeBuild.cmake.arguments` alongside the
  existing `ENGINE_HOST_ASSET_CODEGEN`, for the same reason: cross-compiling for Android needs a
  native `icon_codegen` (§19.3 host tool) the same way it needs a native `asset_codegen`. Missing
  until a downstream game's cross-compiling build hit `CMake Error: Cross-compiling builds need a
  native icon_codegen`.

Because `AndroidManifest.xml`'s `<application>` carries `android:icon="@mipmap/ic_launcher"`
unconditionally, and unlike `android:label` there is no `manifestPlaceholders` equivalent for a
whole mipmap resource, a game that supplies no icon overlay has nothing for that reference to
resolve against — an unresolved manifest resource reference is a hard AAPT2 link error, not a
graceful fallback to a platform default. The engine ships its own default
`mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png` under `cmake/android/app/src/main/res/` specifically
so the reference always resolves even without a per-game overlay.

`engine_add_game`'s icon step (`icon.png` → `icon_codegen` → `ENGINE_GAME_ICON_DIR`, one
`${target}_icons` target shared by every platform) already generates
`<ENGINE_GAME_ICON_DIR>/mipmap-*/ic_launcher.png` for every game with an `icon.png`, but nothing
copies that into `ENGINE_ANDROID_RES_DIR` automatically: Gradle resolves `res.srcDirs` at
configuration time, before the CMake `externalNativeBuild` invocation that runs `icon_codegen` even
starts, so there is no single-invocation way to feed one into the other. A game wires them today by
pointing `ENGINE_ANDROID_RES_DIR` at a directory it populates itself (e.g. copying
`<ENGINE_GAME_ICON_DIR>/mipmap-*` there as a pre-build step) — an automatic bridge between the two
is a possible follow-up, not implemented here.

## See also

- [[build/Pipeline]]
- [[build/CMake]]
- [[build/Icon Codegen]]
- [[features/Init and Loop]]
- [[include.engine.core.engine.h]]
- [[include.engine.igame.h]]
