# Wind

A small, embeddable 2D C++ game engine. CMake target / C++ namespace: `engine`. Task codes: `wind-N`. Design of record: [docs/sdd.md](docs/sdd.md). Obsidian tech vault (architecture, modules, files, build): [docs/tech/README.md](docs/tech/README.md).

## Build (library + tests)

```bash
git submodule update --init --recursive
cmake --preset vs
cmake --build build --target engine_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

SDL / OpenGL / NanoVG stay behind `ENGINE_WITH_WINDOW` (default **OFF** in this repo; local preset `vs-window`). Mixer stays behind `ENGINE_WITH_AUDIO` (default **OFF**; local preset `vs-audio`). `engine_tests` never calls `Engine::run` and never opens a mixer device.

Games consume this repo as a git submodule at `external/engine`:

```cmake
add_subdirectory(external/engine)
engine_add_game(my_game src/main.cpp)
```

`ENGINE_BUILD_TESTS` defaults OFF then. `ENGINE_WITH_WINDOW` defaults **ON** for that subdirectory so a game does not have to FORCE it.

## Build for web (Emscripten / WebGL2)


1. Install [emsdk](https://emscripten.org/docs/getting_started/downloads.html) and activate it (`source emsdk_env.sh` so `emcmake` and `EMSDK` are available).
2. Cook assets with a **native** `asset_codegen` (the WASM compiler cannot run the cook tool as a host binary):

```bash
git submodule update --init --recursive
cmake -S . -B build -DENGINE_BUILD_TESTS=OFF
cmake --build build --target asset_codegen
```

On Visual Studio the binary is `build/Debug/asset_codegen.exe` (or the active config dir). On Ninja/Make it is `build/asset_codegen`.

3. Configure and build the game (or this repo) with Emscripten. `ENGINE_WITH_WEB` defaults **ON** when `EMSCRIPTEN` is set.

```bash
emcmake cmake --preset web -DENGINE_HOST_ASSET_CODEGEN="$PWD/build/asset_codegen"
cmake --build build-web
```

Mixer stays behind `ENGINE_WITH_AUDIO` here too (default **OFF**; local preset `web-audio` builds to `build-web-audio` with it **ON**).

If the `web` preset's Ninja generator is missing, pass `-G "Unix Makefiles"` (or install Ninja). Equivalent without the preset:

```bash
emcmake cmake -S . -B build-web \
  -DENGINE_WITH_WINDOW=ON \
  -DENGINE_WITH_WEB=ON \
  -DENGINE_WITH_AUDIO=OFF \
  -DENGINE_HOST_ASSET_CODEGEN="$PWD/build/asset_codegen"
cmake --build build-web
```

Toolchain file (when `EMSDK` is set, instead of `emcmake`):

```bash
cmake -S . -B build-web \
  --toolchain cmake/toolchains/Emscripten.cmake \
  -DENGINE_WITH_WINDOW=ON -DENGINE_WITH_WEB=ON \
  -DENGINE_HOST_ASSET_CODEGEN="$PWD/build/asset_codegen"
```

A game CMakeLists stays:

```cmake
add_subdirectory(external/engine)
engine_add_game(my_game src/main.cpp)
# optional: engine_add_web_game(my_game src/main.cpp)  # fatal if not Emscripten
```

On Emscripten, `engine_add_game` emits `my_game.html` / `.js` / `.wasm` / `.data`, preloads `assets/` at `/assets`, and links WebGL2 (`USE_WEBGL2`, `FULL_ES3`, `ALLOW_MEMORY_GROWTH`). Override the HTML shell with `-DENGINE_WEB_SHELL=/path/to/shell.html`.

4. Serve over HTTP (file:// often blocks WASM):

```bash
python3 -m http.server -d build-web/bin
```

Open the game HTML in a browser. Default `engine_tests` in this repo stay **headless native**; they do not boot `Engine::run` or WebGL. Mixer stays off on the `web` preset (`ENGINE_WITH_AUDIO=OFF`).

## Build for Android (NDK / GLES / APK)


1. Install the [Android SDK](https://developer.android.com/studio) and [NDK](https://developer.android.com/ndk) (SDL3 wants API **21+**, NDK r28c+ recommended). Set `ANDROID_NDK` (or `ANDROID_NDK_HOME`) and `ANDROID_HOME`.
2. Cook assets with a **native** `asset_codegen` (the NDK compiler cannot run the cook tool as a host binary):

```bash
git submodule update --init --recursive
cmake -S . -B build -DENGINE_BUILD_TESTS=OFF
cmake --build build --target asset_codegen
```

On Visual Studio the binary is `build/Debug/asset_codegen.exe` (or the active config dir). On Ninja/Make it is `build/asset_codegen`.

3. Optional: configure the native shared library with the NDK toolchain to compile-check `libmain.so`. `ENGINE_WITH_ANDROID` and `ENGINE_WITH_GLES` default **ON** when `ANDROID` is set. This does **not** populate APK assets for Gradle.

```bash
cmake --preset android-arm64 \
  -DENGINE_HOST_ASSET_CODEGEN="$PWD/build/asset_codegen"
cmake --build build-android
```

Equivalent without the preset:

```bash
cmake -S . -B build-android \
  --toolchain cmake/toolchains/android-ndk.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21 \
  -DENGINE_WITH_WINDOW=ON \
  -DENGINE_WITH_ANDROID=ON \
  -DENGINE_WITH_AUDIO=OFF \
  -DENGINE_HOST_ASSET_CODEGEN="$PWD/build/asset_codegen"
cmake --build build-android
```

That produces `libmain.so` (and shared SDL3) under `build-android/` and stages cooked assets beside the library for compile checks.

4. Build an APK with the Gradle template in `cmake/android/`. Point Java sources at the SDL3 submodule (`external/SDL3/android-project/...`). `assembleDebug` runs CMake via `externalNativeBuild` and stages cooked assets to the app module's `build/wind-assets/` (`-DENGINE_ANDROID_ASSETS_OUT`, same path as `sourceSets.main.assets.srcDirs`). From a **game** repo (engine at `external/engine`):

```cmake
add_subdirectory(external/engine)
engine_add_game(my_game src/main.cpp)
# optional: engine_add_android_game(my_game src/main.cpp)  # fatal if not ANDROID
```

```bash
cd external/engine/cmake/android
# Generate or copy a Gradle wrapper (Android Studio: Open this folder), then:
./gradlew :app:assembleDebug \
  -PENGINE_SOURCE_DIR="$(pwd)/../.." \
  -PENGINE_ANDROID_CMAKE=/path/to/game/CMakeLists.txt \
  -PENGINE_HOST_ASSET_CODEGEN=/path/to/native/asset_codegen
```

Change `applicationId` / `namespace` (`org.windengine.app`) and `app_name` before shipping. v1 ABI is **arm64-v8a**, minSdk **21**. Mixer stays off on the `android-arm64` preset (`ENGINE_WITH_AUDIO=OFF`).

Default `engine_tests` in this repo stay **headless native**; they do not boot `Engine::run`, EGL, or a mixer. An emulator/GPU golden is not a merge gate.
