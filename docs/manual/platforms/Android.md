# Android

Wind games compile to Android using the Android NDK, CMake, and Gradle, producing an APK.

> [!NOTE]
> The editor's Export button builds desktop games only. An Android build is a Gradle build of the template in the SDK's engine source (`<sdk>/source/cmake/android`), which builds the engine and your game with CMake. The commands below follow the engine's build scripts and `README.md`; they have not been run end to end for this manual.

---

## 1. Prerequisites

- **Android SDK & NDK.** The Gradle template asks for NDK `28.2.13676358`, `compileSdk` 35, `minSdk` 21, and the Android Gradle plugin 8.7.3. Set `ANDROID_HOME`.
- **Java 17+**
- **Gradle.** The template carries only `gradle-wrapper.properties` (Gradle 8.9), not the wrapper scripts: open `<sdk>/source/cmake/android` in Android Studio or generate a wrapper there first.
- **Native codegen tools.** The NDK compiler cannot run the cooking tools, so the build is given native `asset_codegen` and `icon_codegen` executables. Both ship in the SDK: `<sdk>/bin/asset_codegen` and `<sdk>/bin/icon_codegen` (with `.exe` on Windows).

---

## 2. Project Setup

The game's `CMakeLists.txt` adds the engine's source from the SDK for Android, like it does for [Web](Web-Wasm.md#2-the-games-cmakeliststxt), and keeps `find_package(Wind)` for desktop:

```cmake
if(ANDROID)
    # The Gradle build cannot pass a -D to this file: the SDK path comes from the environment
    add_subdirectory("$ENV{WIND_SDK_DIR}/source" wind)
else()
    find_package(Wind REQUIRED)
endif()

engine_add_game(my_game src/main.cpp src/game.cpp)   # engine_add_android_game is the same call, Android only
```

Then run Gradle in the SDK's template. Everything the template reads is a `-P` property or the same-named environment variable:

```bash
export WIND_SDK_DIR=/path/to/wind-engine/out/sdk
cd "$WIND_SDK_DIR/source/cmake/android"
./gradlew :app:assembleDebug \
    -PENGINE_SOURCE_DIR="$WIND_SDK_DIR/source" \
    -PENGINE_ANDROID_CMAKE=/path/to/my_game/CMakeLists.txt \
    -PENGINE_HOST_ASSET_CODEGEN="$WIND_SDK_DIR/bin/asset_codegen" \
    -PENGINE_HOST_ICON_CODEGEN="$WIND_SDK_DIR/bin/icon_codegen" \
    -PENGINE_ANDROID_APPLICATION_ID=com.mystudio.mygame \
    -PENGINE_ANDROID_APP_NAME="My Super Game"
```

| Property | Effect | Default |
| --- | --- | --- |
| `ENGINE_SOURCE_DIR` | the engine source (`<sdk>/source`), for the SDL Java sources | three directories above the template |
| `ENGINE_ANDROID_CMAKE` | the `CMakeLists.txt` Gradle builds | the engine's own |
| `ENGINE_HOST_ASSET_CODEGEN`, `ENGINE_HOST_ICON_CODEGEN` | the native tools | required |
| `ENGINE_ANDROID_APPLICATION_ID` | the application id | `org.windengine.app` |
| `ENGINE_ANDROID_APP_NAME` | the launcher label | `Wind` |
| `ENGINE_ANDROID_RES_DIR` | extra resources (launcher icons `mipmap-*/ic_launcher.png`) | none |
| `ENGINE_ANDROID_MANIFEST` | a manifest overlay merged over the template's | none |
| `ENGINE_WITH_AUDIO` | `ON` or `OFF` | `ON` |

Two games that keep the defaults share an application id and a name, so set both. A manifest overlay can also sit next to your `CMakeLists.txt` as `android/AndroidManifest.xml`. The template already asks for the `INTERNET` and `VIBRATE` permissions. From Android 9 plain `http://` is blocked; allow it in your manifest overlay if your game needs it. The ABI is `arm64-v8a`.

> [!NOTE]
> Passing `WIND_SDK_DIR` through the environment into the Gradle-started CMake is the part of this setup that was not checked.

---

## 3. How Android Execution Operates

1. **Target:** On Android, `engine_add_game` builds a shared native library `libmain.so`, with the SDL entry point aliased to your `main`.
2. **Assets Staging:** Gradle packs the cooked assets into the APK's `assets/`. At first launch, the engine copies them to internal storage so standard C++ `std::ifstream` streams can load them. `engine::user_data_directory` is `<internal storage>/user/`.
3. **Graphics API:** Wind configures an OpenGL ES 3.0 context (`GraphicsProfile::Api::Gles3`).
4. **App Lifecycle:** When the app goes to the background (home button, phone call), the engine pauses the fixed simulation (`ApplicationState::paused`); it resumes when the app is back.
5. **Back Button:** The engine never quits on Back. The key arrives as `engine::KeyCode::AcBack`, like any other key, and your game decides what it does (see below).

### The back button

Bind `KeyCode::AcBack` to an action of your own and handle it like any other action: pop the current screen, and quit only from the root screen. If you do not bind it, Back does nothing.

```cpp
#include <engine/core/input_system.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>

void MyGame::on_start() {
    back_ = services_.input.intern("back");
    services_.input.bind(engine::KeyCode::AcBack, back_);
    services_.input.bind(engine::KeyCode::Escape, back_); // same action on desktop

    world().add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game, [this](engine::ecs::World& world) {
        for (const engine::InputEvent& event : engine::ecs::EventReader<engine::InputEvent>{
                     world, world.ctx<engine::ecs::EventCursor<engine::InputEvent>>()}) {
            if (event.action != back_ || event.kind != engine::InputEvent::Kind::Down) {
                continue;
            }
            if (screens_.size() > 1) {
                pop_screen();
            } else {
                worlds().application_state().quit();
            }
        }
    });
}
```

While a `TextInput` has the soft keyboard open, Back only closes the keyboard (the field loses focus). That press never reaches the action, so the game does not also navigate back. The next press is delivered as usual.

---

## 4. App Icons (`icon.png`)

Place an `icon.png` at the root of your game, next to the `CMakeLists.txt` that calls `engine_add_game`. It must be square and at least 1024x1024 pixels, or the build fails. `icon_codegen` writes, under `<build>/generated/<game>/icons/`, the Windows and macOS icons, the web `favicon.png`, and the five Android launcher sizes (`mipmap-mdpi/ic_launcher.png` to `mipmap-xxxhdpi/ic_launcher.png`).

Gradle reads its resources before that build step runs, so the launcher icons are not picked up automatically: copy the `mipmap-*` folders into a directory of your own and pass it as `-PENGINE_ANDROID_RES_DIR`. Without it the APK keeps the engine's default icon.

---

## Next Steps

- Review C++ development standards in [Rules & Conventions](../best-practices/Rules-and-Conventions.md).
- Avoid common pitfalls in [Common Pitfalls](../best-practices/Common-Pitfalls.md).
