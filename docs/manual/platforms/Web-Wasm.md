# Web (WebAssembly)

Wind games compile to WebAssembly and WebGL2 using the Emscripten SDK (`emsdk`).

> [!NOTE]
> The editor's Export button builds desktop games only. A Web build is a separate configure with the Emscripten toolchain, and it builds the engine from the SDK's engine source (`<sdk>/source`, the directory the SDK ships for exactly this). The commands below follow the engine's build scripts and `README.md`; they have not been run end to end for this manual.

---

## 1. Prerequisites

1. Install and activate [emsdk](https://emscripten.org/docs/getting_started/downloads.html):
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. **Native codegen tools.** The WebAssembly compiler cannot run the asset and icon cooking tools, because they would be built for WASM. A cross-compiling configure refuses to run unless it is given native `asset_codegen` and `icon_codegen` executables. Both are in the Wind SDK:
   - `<sdk>/bin/asset_codegen` (`asset_codegen.exe` on Windows)
   - `<sdk>/bin/icon_codegen` (`icon_codegen.exe` on Windows)

   You do not have to build anything for them.

---

## 2. The Game's `CMakeLists.txt`

A game that you play in the editor finds the SDK with `find_package(Wind)`, which imports the SDK's native shared `engine.dll`. That cannot run in a browser, so for Web the game adds the engine's source instead:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_game LANGUAGES CXX)

if(EMSCRIPTEN)
    # Web builds the engine from the SDK's source, as a static library
    add_subdirectory("${WIND_SDK_DIR}/source" wind)
else()
    find_package(Wind REQUIRED)
endif()

engine_add_game(my_game
    src/main.cpp
    src/game.h
    src/game.cpp
)
```

`WIND_SDK_DIR` is the SDK directory (the one with `sdk.toml`), passed on the command line. `engine_add_game` makes an executable that Emscripten links as `my_game.html` with its `.js`, `.wasm`, and `.data`; `engine_add_web_game` is the same call and fails the configure when the toolchain is not Emscripten. The `assets/` folder and the cooked catalogs are preloaded into the virtual file system at `/assets`.

---

## 3. Configure and Build

```bash
emcmake cmake -B build-web -S . \
    -DWIND_SDK_DIR="/path/to/wind-engine/out/sdk" \
    -DENGINE_HOST_ASSET_CODEGEN="/path/to/wind-engine/out/sdk/bin/asset_codegen" \
    -DENGINE_HOST_ICON_CODEGEN="/path/to/wind-engine/out/sdk/bin/icon_codegen" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build-web
```

When the engine is added as a subdirectory it builds with the window (SDL3 and WebGL2) on, and the audio mixer (`ENGINE_WITH_AUDIO`) on too. If you do not want it, add `-DENGINE_WITH_AUDIO=OFF` ([CMake Integration](../getting-started/CMake-Integration.md#disabling-audio-headless-or-audio-free-titles)). The engine's own `web` preset leaves the mixer off; the `web-audio` preset is the one that turns it on, so test your game's sound in a browser before relying on it. If Ninja is missing, pass `-G "Unix Makefiles"`.

An optional `-DENGINE_WEB_SHELL=/path/to/shell.html` replaces the default HTML page (`cmake/web/shell.html` of the SDK source).

---

## 4. Testing Locally

WebAssembly applications must be served over an HTTP server (browsers often block `file://`). The outputs are in `build-web/bin/`:

```bash
python3 -m http.server 8080 --directory build-web/bin
```

Open `http://localhost:8080/my_game.html` in your browser.

---

## 5. Web-Specific Considerations

- **User Gesture for Audio:** Browsers keep audio locked until the player interacts with the page. The engine does not unlock it by itself ([Sound & Music](../audio-and-haptics/Sound-and-Music.md#5-web)).
- **IndexedDB Persistence:** `engine::user_data_directory` is `/storage/<organization>/<application>/`, mounted to IndexedDB. The browser flushes writes a few frames later.
- **Frame pacing:** The loop runs on the browser's animation frame. `set_vsync` and `set_max_fps` have no effect.
- **Haptics:** `navigator.vibrate` where the browser has it; many do not.

---

## Next Steps

- Package for mobile devices in [Android](Android.md).
- Follow essential C++ guidelines in [Rules & Conventions](../best-practices/Rules-and-Conventions.md).
