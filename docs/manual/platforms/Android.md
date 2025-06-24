# Android

Wind games compile to Android using the Android NDK, CMake, and Gradle, producing signed APKs and AAB bundles.

---

## 1. Prerequisites

- **Android SDK & NDK** (r25+ recommended)
- **Java 17+**
- Host `asset_codegen` and `icon_codegen` binaries built natively (similar to the Web build requirement).

---

## 2. Project Setup

Wind provides an Android project template located in `cmake/android/app` of the engine source (`<sdk>/source/cmake/android/app`; an Android build adds that source with `add_subdirectory`, see [Game Consumer](../../tech/build/Game%20Consumer.md#standalone-executable)). In your game repository's `build.gradle`, set the key properties:

```groovy
// build.gradle / gradle.properties
ENGINE_ANDROID_APPLICATION_ID = "com.mystudio.mygame"
ENGINE_ANDROID_APP_NAME = "My Super Game"
ENGINE_HOST_ASSET_CODEGEN = "/path/to/native/asset_codegen"
ENGINE_HOST_ICON_CODEGEN = "/path/to/native/icon_codegen"
```

---

## 3. How Android Execution Operates

1. **Target:** On Android, `engine_add_game` builds a shared native library `libmain.so`.
2. **Assets Staging:** The build packages cooked assets into Android's APK assets (`assets/`). At first launch, the engine stages them to internal storage so standard C++ `std::ifstream` streams can load them.
3. **Graphics API:** Wind configures an OpenGL ES 3.0 context (`GraphicsProfile::Api::Gles3`).
4. **App Lifecycle:** Pausing the app (home button, phone call) invokes lifecycle callbacks and pauses the simulation loop automatically.
5. **Back Button:** The engine never quits on Back. The key arrives as `engine::KeyCode::AcBack`, like any other key, and your game decides what it does (see below).

### The back button

Bind `KeyCode::AcBack` to an action of your own and handle it like any other action: pop the current screen, and quit only from the root screen. If you do not bind it, Back does nothing.

```cpp
#include <engine/core/application_state.h>
#include <engine/core/input_system.h>
#include <engine/ecs/events.h>

void MyGame::on_start() {
    back_ = services_.input.intern("back");
    services_.input.bind(engine::KeyCode::AcBack, back_);
    services_.input.bind(engine::KeyCode::Escape, back_); // same action on desktop

    world_.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game, [this](engine::ecs::World& world) {
        for (const engine::InputEvent& event : engine::ecs::EventReader<engine::InputEvent>{
                     world, world.ctx<engine::ecs::EventCursor<engine::InputEvent>>()}) {
            if (event.action != back_ || event.kind != engine::InputEvent::Kind::Down) {
                continue;
            }
            if (screens_.size() > 1) {
                pop_screen();
            } else {
                world.ctx<engine::ApplicationState>().quit();
            }
        }
    });
}
```

While a `TextInput` has the soft keyboard open, Back only closes the keyboard (the field loses focus). That press never reaches `InputSystem`, so the game does not also navigate back. The next press is delivered as usual.

---

## 4. App Icons (`icon.png`)

Place an `icon.png` (512x512 or 1024x1024) at the root of your game repository. During the build, `icon_codegen` generates:
- `mipmap-mdpi/ic_launcher.png`
- `mipmap-hdpi/ic_launcher.png`
- `mipmap-xhdpi/ic_launcher.png`
- `mipmap-xxhdpi/ic_launcher.png`
- `mipmap-xxxhdpi/ic_launcher.png`

---

## Next Steps

- Review C++ development standards in [Rules & Conventions](../best-practices/Rules-and-Conventions.md).
- Avoid common pitfalls in [Common Pitfalls](../best-practices/Common-Pitfalls.md).
