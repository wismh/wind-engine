# CMake

Root file: `CMakeLists.txt`. Minimum version 3.16. Language C and CXX. `CMAKE_CXX_STANDARD` is 23 with extensions OFF.

The library target is `engine`. Alias: `engine::engine`.

## Options

`_engine_is_root` is ON when this directory is the CMake source root.

| Option | Engine root | `add_subdirectory` | Notes |
| --- | --- | --- | --- |
| `ENGINE_BUILD_TESTS` | ON | OFF | Builds `engine_tests` |
| `ENGINE_WITH_GTEST` | follows tests | follows tests | Vendors `external/googletest` without building `engine_tests`. `ENGINE_BUILD_TESTS` forces it ON |
| `ENGINE_WITH_WINDOW` | OFF | ON | SDL3, OpenGL or GLES, NanoVG. `PUBLIC` define `ENGINE_WITH_WINDOW=1` |
| `ENGINE_WITH_AUDIO` | OFF | ON | SDL3_mixer, WAV only. `PRIVATE` define |
| `ENGINE_WITH_WEB` | ON if `EMSCRIPTEN`, else OFF | same | `PUBLIC` |
| `ENGINE_WITH_ANDROID` | ON if `ANDROID`, else OFF | same | `PUBLIC` |
| `ENGINE_WITH_GLES` | ON if `EMSCRIPTEN` or `ANDROID`, else OFF | same | `PUBLIC`. No glad |

`ENGINE_UI_PROFILER` and `ENGINE_CLI_SERVER` are not options. They are generator expressions on Debug and RelWithDebInfo. The CLI define is also omitted for Emscripten and Android. See [Boundaries](../architecture/Boundaries.md).

Cache paths:

| Variable | Role |
| --- | --- |
| `ENGINE_HOST_ASSET_CODEGEN` | Native `asset_codegen` when `CMAKE_CROSSCOMPILING` |
| `ENGINE_HOST_ICON_CODEGEN` | Native `icon_codegen` when cross-compiling |
| `ENGINE_WEB_SHELL` | HTML shell. Empty uses `cmake/web/shell.html` |
| `ENGINE_ANDROID_ASSETS_OUT` | Directory Gradle packs. Empty stages only beside `libmain.so` |

## Presets

`CMakePresets.json`. Generators are Visual Studio 18 2026 (`vs`, `vs-window`, `vs-audio`) or Visual Studio 17 2022 (`vs2022`). Web and Android use Ninja.

| Preset | Binary dir | Tests | Window | Audio | Also |
| --- | --- | --- | --- | --- | --- |
| `vs` | `build` | ON | OFF | OFF | |
| `vs2022` | `build` | ON | OFF | OFF | VS 2022 generator |
| `vs-window` | `build-window` | ON | ON | OFF | |
| `vs-audio` | `build-audio` | OFF | ON | ON | |
| `web` | `build-web` | ON | ON | OFF | `ENGINE_WITH_WEB=ON`. Configure with `emcmake` |
| `web-audio` | `build-web-audio` | OFF | ON | ON | `ENGINE_WITH_WEB=ON` |
| `android-arm64` | `build-android` | ON | ON | OFF | toolchain `cmake/toolchains/android-ndk.cmake`, `arm64-v8a`, `android-21`, `ENGINE_WITH_ANDROID=ON` |

Build preset `tests` builds `engine_tests` from the `vs` configure.

## What links

Always, PRIVATE: spdlog, tinyxml2, tomlplusplus. PUBLIC: glm.

Windows also links `ws2_32` and `advapi32` PUBLIC, because `cli_server.cpp` uses WinSock and ACL APIs and a static library does not propagate PRIVATE system libs.

`ENGINE_WITH_WINDOW` adds SDL3 (static, except Android where SDL is shared), nanovg, and either glad plus `OpenGL::GL` or, with GLES, SDL and nanovg (Android also GLESv3, EGL, `android`, `log`).

`ENGINE_WITH_AUDIO` adds SDL3 if it is not already there, then SDL3_mixer with every format except WAVE turned OFF. `SDLMIXER_VENDORED` is OFF.

`engine_add_sdl3` on Emscripten forces `SDL_EMSCRIPTEN_PERSISTENT_PATH` to `/storage` so `user_data_directory` is not an in-memory `/libsdl`.

## `engine_add_game`

Call it after `add_subdirectory` of this repo. At least one source file. `ENGINE_WITH_WINDOW` must be ON (the subdirectory default).

| Platform | Target |
| --- | --- |
| Desktop | `add_executable` |
| Android | `add_library` SHARED, `OUTPUT_NAME` `main`, plus `--defsym=SDL_main=main` and `SDL3::SDL3main` when that target exists |
| Apple | `MACOSX_BUNDLE` ON |

C++23, no extensions. MSVC: `/W4 /permissive- /utf-8`. MSVC Windows, every config except Debug: `/SUBSYSTEM:WINDOWS` and `/ENTRY:mainCRTStartup` so there is no console and `main` stays the entry.

The game links `engine` PRIVATE. If the game has an `include/` directory it is PRIVATE too. `engine/src` is not on the game's include path.

Asset and icon steps: [Pipeline](Pipeline.md). Web adds `cmake/web/link_flags.cmake` (`engine_target_web_link_options`, `engine_target_web_preload`) and copies `favicon.png` beside the output when icons were generated.

`engine_add_web_game` and `engine_add_android_game` call `engine_add_game` and fail the configure when `EMSCRIPTEN` or `ANDROID` is not set.

## Tests

`engine_tests` globs `tests/*_test.cpp`, links `engine` and `GTest::gtest_main`, and adds `src/` as a PRIVATE include so a test can reach a private header. With the window flag it also links SDL3 and, unless GLES, glad.

Compile definitions: `ENGINE_BUILTIN_ASSETS_DIR`, `ENGINE_SOURCE_DIR`.

`engine_prepare_runtime(engine_tests)` copies builtin assets beside the test binary. Desktop uses `gtest_discover_tests` with `DISCOVERY_MODE PRE_TEST`.

## Host tools

Not cross-compiling: `asset_codegen`, `asset_guid` (link `engine`), `wind-cli` (does not link `engine`; WinSock on Windows), `icon_codegen` (links `engine` and adds `src/` so it can include `resources/icon_codegen.h`).

Cross-compiling: `asset_codegen` and `icon_codegen` are `IMPORTED` from the host-path cache variables. `asset_guid` and `wind-cli` are not built in that configure.

## See also

- [Game Consumer](Game Consumer.md)
- [Boundaries](../architecture/Boundaries.md)
