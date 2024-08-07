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
`app_name`, `applicationId`, and launcher icon. `cmake/android/app/build.gradle` resolves two more
`-P` properties, mirroring `ENGINE_ANDROID_ASSETS_OUT`:

- `ENGINE_ANDROID_RES_DIR` — a directory the game supplies (`mipmap-*/ic_launcher.png`, optionally
  `values/strings.xml` with its own `app_name`). Added to `sourceSets.main.res.srcDirs`, so AGP's
  standard resource merger overlays it over the engine's own `res/` by resource name — the same
  technique Unity's generated Android project uses. The committed engine template is never edited
  per game.
- `ENGINE_ANDROID_APPLICATION_ID` — overrides `defaultConfig.applicationId`; defaults to
  `org.windengine.app` when absent.

`AndroidManifest.xml`'s `<application>` carries `android:icon="@mipmap/ic_launcher"` unconditionally
— it simply doesn't resolve (falls back to the platform default) when no overlay supplies that
mipmap, the same behavior `android:label="@string/app_name"` already has without a `strings.xml`
overlay.

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
- [[features/Init and Loop]]
- [[include.engine.core.engine.h]]
- [[include.engine.igame.h]]
