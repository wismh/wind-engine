# Wind

A 2D C++ game engine for production titles. CMake target / C++ namespace: `engine`. Task codes: `wind-N`. Tech vault: [docs/tech/README.md](docs/tech/README.md). Manual: [docs/manual/README.md](docs/manual/README.md). Architecture: [Principles](docs/tech/architecture/Principles.md), [Scope](docs/tech/architecture/Scope.md), [Boundaries](docs/tech/architecture/Boundaries.md).

## Build (library + tests)

```bash
git submodule update --init --recursive
cmake --preset vs
cmake --build build --target engine_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

SDL / OpenGL / NanoVG stay behind `ENGINE_WITH_WINDOW` (default **OFF** in this repo; local preset `vs-window`). Mixer stays behind `ENGINE_WITH_AUDIO` (default **OFF**; local preset `vs-audio`). `engine_tests` never calls `Engine::run` and never opens a mixer device.

Editor build: `ENGINE_EDITOR=ON` makes `engine` a shared library (`engine.dll` beside the executables) and builds the editor, `wind_editor`. The `vs-editor` preset turns it on with the window and audio backends and builds to `build-editor`:

```bash
cmake --preset vs-editor
cmake --build build-editor --config Debug
ctest --test-dir build-editor -C Debug --output-on-failure
```

Editor SDK: install one configuration of the editor build (`/out/` is gitignored). It holds `wind_editor.exe`, `engine.dll`, the host tools, their `.pdb` files, their assets, `engine.lib`, the public headers with that build's `build_id.h`, GoogleTest, the CMake package a game finds (`find_package(Wind)`), the engine source (`source/`), and `sdk.toml` (version, commit, build id). The engine version is `project(engine VERSION ...)` in `CMakeLists.txt`. The editor build's Release is optimized with symbols and keeps the Profiler tab and the `wind-cli` server:

```bash
cmake --build build-editor --config Release
cmake --install build-editor --config Release --prefix out/sdk
```

A game builds its module against the SDK: its `CMakeLists.txt` calls `find_package(Wind REQUIRED)`, and it is configured with `-DCMAKE_PREFIX_PATH=<absolute path to out/sdk>` and `-DCMAKE_CONFIGURATION_TYPES=DebugGame;Release`, usually from its `editor` preset and `CMakeUserPresets.json` (see [Game Consumer](docs/tech/build/Game%20Consumer.md#editor-module)). That configure compiles no engine and gives `bin/<config>/<game>.dll` with its `assets/`. `DebugGame` is game code at `/Od` with symbols and the release CRT (`/MD`), so it loads into the Release editor; a module with the debug CRT does not compile. `ENGINE_EDITOR=ON` in a game tree is a configure error. The editor refuses a module built against another build id:

```bash
out/sdk/bin/wind_editor.exe                                # opens a file dialog: pick the project's wind_project.toml
out/sdk/bin/wind_editor.exe --project path/to/my_game      # no dialog
out/sdk/bin/wind_editor.exe --project path/to/my_game --play  # build, then play
```

Play loads a copy of the module and runs the game in the "Game" window. Stop unloads it, so the game can be rebuilt while the editor stays open. See [Editor](docs/tech/features/Editor.md).

Wind Launcher lists your projects and installed editors and opens a project in the editor of its version ([Launcher](docs/tech/features/Launcher.md)):

```bash
cmake --preset vs-launcher
cmake --build build-launcher --config Release
cmake --install build-launcher --config Release --prefix out/launcher
out/launcher/bin/wind_launcher.exe
```

Exported games, web, and Android stay on the static library. See [CMake](docs/tech/build/CMake.md#editor-build).

Games do not keep this repo as a submodule. They find an installed SDK (above). A standalone executable, Web, or Android build adds the SDK's engine source instead, until the editor exports ([Game Consumer](docs/tech/build/Game%20Consumer.md#standalone-executable)):

```cmake
add_subdirectory(path/to/sdk/source wind)
engine_add_game(my_game src/main.cpp)
```

`ENGINE_BUILD_TESTS` defaults OFF then, so `engine_tests` is not even configured as a target. If the game wants GoogleTest for its own tests without pulling in the engine's internal suite, set `ENGINE_WITH_GTEST ON` instead — `ENGINE_BUILD_TESTS` still implies it, but not the other way around. `ENGINE_WITH_WINDOW` and `ENGINE_WITH_AUDIO` both default **ON** for that subdirectory so a game does not have to FORCE either.

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

A game's standalone CMakeLists adds the SDK's engine source:

```cmake
add_subdirectory(path/to/sdk/source wind)
engine_add_game(my_game src/main.cpp)
# optional: engine_add_web_game(my_game src/main.cpp)  # fatal if not Emscripten
```

On Emscripten, `engine_add_game` emits `my_game.html` / `.js` / `.wasm` / `.data`, preloads `assets/` at `/assets`, and links WebGL2 (`USE_WEBGL2`, `FULL_ES3`, `ALLOW_MEMORY_GROWTH`). Override the HTML shell with `-DENGINE_WEB_SHELL=/path/to/shell.html`. `engine_add_sdl3` mounts IndexedDB at `/storage` so `user_data_directory` survives a reload; the browser flushes that write over the next few frames.

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

4. Build an APK with the Gradle template in `cmake/android/`. Point Java sources at the SDL3 submodule (`external/SDL3/android-project/...`). `assembleDebug` runs CMake via `externalNativeBuild` and stages cooked assets to the app module's `build/wind-assets/` (`-DENGINE_ANDROID_ASSETS_OUT`, same path as `sourceSets.main.assets.srcDirs`). From a **game** repo (engine source at `<sdk>/source`):

```cmake
add_subdirectory(path/to/sdk/source wind)
engine_add_game(my_game src/main.cpp)
# optional: engine_add_android_game(my_game src/main.cpp)  # fatal if not ANDROID
```

```bash
cd path/to/sdk/source/cmake/android
# Generate or copy a Gradle wrapper (Android Studio: Open this folder), then:
./gradlew :app:assembleDebug \
  -PENGINE_SOURCE_DIR="$(pwd)/../.." \
  -PENGINE_ANDROID_CMAKE=/path/to/game/CMakeLists.txt \
  -PENGINE_HOST_ASSET_CODEGEN=/path/to/native/asset_codegen
```

Change `applicationId` / `namespace` (`org.windengine.app`) before shipping. The launcher label is `android:label="${appName}"`, set by `-PENGINE_ANDROID_APP_NAME` or the `ENGINE_ANDROID_APP_NAME` environment variable (default `Wind`). `res/values/strings.xml` still has `app_name`; the manifest does not reference it.

v1 ABI is **arm64-v8a**, minSdk **21**. Mixer stays off on the `android-arm64` preset (`ENGINE_WITH_AUDIO=OFF`).

Default `engine_tests` in this repo stay **headless native**; they do not boot `Engine::run`, EGL, or a mixer. An emulator/GPU golden is not a merge gate.
