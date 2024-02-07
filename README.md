# Wind


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
