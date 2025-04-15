# Android

Wind games compile to Android using the Android NDK, CMake, and Gradle, producing signed APKs and AAB bundles.

---

## 1. Prerequisites

- **Android SDK & NDK** (r25+ recommended)
- **Java 17+**
- Host `asset_codegen` and `icon_codegen` binaries built natively (similar to the Web build requirement).

---

## 2. Project Setup

Wind provides an Android project template located in `external/engine/cmake/android/app`. In your game repository's `build.gradle`, set the key properties:

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
5. **Back Button:** Pressing the Android back button routes as a back action event.

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
