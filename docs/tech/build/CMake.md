# CMake

Root file: `CMakeLists.txt`. Minimum version 3.20 (the build id step uses `$<CONFIG>` in a custom command output). `project(engine VERSION x.y.z)` is the engine version, semver; see [Version](#version). Language C and CXX. `CMAKE_CXX_STANDARD` is 23 with extensions OFF. The game functions (`engine_add_game` and the rest) live in `cmake/wind_game.cmake`, which the root file includes. See [Game functions](#game-functions).

The library target is `engine`. Alias: `engine::engine`. It is static. With `ENGINE_EDITOR` it is shared.

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
| `ENGINE_LAUNCHER` | OFF | configure error | `wind_launcher` and its install rules ([Launcher build](#launcher-build)). Needs `ENGINE_WITH_WINDOW`, refuses `ENGINE_EDITOR`, Emscripten, and Android |
| `ENGINE_EDITOR` | OFF | configure error | Shared `engine`, the editor, the SDK install rules. Needs `ENGINE_WITH_WINDOW`. Fatal on Emscripten and Android. In a game tree it is fatal and points at `find_package(Wind)` |

`ENGINE_UI_PROFILER` and `ENGINE_CLI_SERVER` are not options. Without `ENGINE_EDITOR` they are generator expressions on Debug and RelWithDebInfo. With `ENGINE_EDITOR` they are on in every configuration, so the Release editor and SDK have the Profiler tab and `wind-cli`. The CLI define is also omitted for Emscripten and Android. See [Boundaries](../architecture/Boundaries.md).

Cache paths:

| Variable | Role |
| --- | --- |
| `ENGINE_HOST_ASSET_CODEGEN` | Native `asset_codegen` when `CMAKE_CROSSCOMPILING` |
| `ENGINE_HOST_ICON_CODEGEN` | Native `icon_codegen` when cross-compiling |
| `ENGINE_WEB_SHELL` | HTML shell. Empty uses `cmake/web/shell.html` |
| `ENGINE_ANDROID_ASSETS_OUT` | Directory Gradle packs. Empty stages only beside `libmain.so` |

## Presets

`CMakePresets.json`. Generators are Visual Studio 18 2026 (`vs`, `vs-window`, `vs-audio`, `vs-editor`) or Visual Studio 17 2022 (`vs2022`). Web and Android use Ninja.

| Preset | Binary dir | Tests | Window | Audio | Also |
| --- | --- | --- | --- | --- | --- |
| `vs` | `build` | ON | OFF | OFF | |
| `vs2022` | `build` | ON | OFF | OFF | VS 2022 generator |
| `vs-window` | `build-window` | ON | ON | OFF | |
| `vs-audio` | `build-audio` | OFF | ON | ON | |
| `vs-editor` | `build-editor` | ON | ON | ON | `ENGINE_EDITOR=ON`. Audio is on so the editor plays game sound. `engine_tests` still opens no mixer device: the audio tests never call `AudioSystem::init` |
| `vs-launcher` | `build-launcher` | ON | ON | OFF | `ENGINE_LAUNCHER=ON`, static engine |
| `web` | `build-web` | ON | ON | OFF | `ENGINE_WITH_WEB=ON`. Configure with `emcmake` |
| `web-audio` | `build-web-audio` | OFF | ON | ON | `ENGINE_WITH_WEB=ON` |
| `android-arm64` | `build-android` | ON | ON | OFF | toolchain `cmake/toolchains/android-ndk.cmake`, `arm64-v8a`, `android-21`, `ENGINE_WITH_ANDROID=ON` |

Build preset `tests` builds `engine_tests` from the `vs` configure.

## Editor build

`ENGINE_EDITOR` is OFF by default. When it is ON:

- `engine` is `SHARED` with `WINDOWS_EXPORT_ALL_SYMBOLS`. Windows output is `engine.dll` in the runtime output directory, beside `engine_tests` and the host tools.
- `ENGINE_SHARED=1` and `ENGINE_EDITOR=1` are `PUBLIC` defines and `ENGINE_BUILDING` a `PRIVATE` one. `ENGINE_API` in `include/engine/core/export.h` reads `ENGINE_SHARED` and `ENGINE_BUILDING`. `ENGINE_EDITOR` reaches `wind_editor`, `engine_tests`, the fixture modules, and through the SDK every game module. Game code may test it only for tools ([Principles](../architecture/Principles.md)).
- `ENGINE_UI_PROFILER` and `ENGINE_CLI_SERVER` are on in every configuration.
- `CMAKE_MSVC_RUNTIME_LIBRARY` is `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL` (the CMake default, made explicit): `/MDd` in Debug, `/MD` otherwise. `<engine/build_id.h>` records it for the [CRT guard](#crt-guard).
- MSVC Release has symbols: `/Zi` on every compile and `/DEBUG /OPT:REF /OPT:ICF` on every link in the engine's tree (`engine.dll`, `wind_editor`, the host tools, `engine_tests`, the fixture modules, and the static libraries inside `engine.dll`). The code is the same `/O2 /Ob2` Release code; `/OPT:REF /OPT:ICF` are given because `/DEBUG` turns them off. `.pdb` files land beside the binaries, and the SDK installs them.
- `CMAKE_POSITION_INDEPENDENT_CODE` is ON, so the static libraries linked into the shared engine are position independent.
- `engine_prepare_runtime` copies `engine.dll` beside its target on Windows.
- `engine_add_game` builds the game as a shared module, not an executable. See below.
- `editor/` is added: `wind_editor`, and with tests also `wind_editor_tests`. See [Editor](../features/Editor.md).
- `ENGINE_WITH_GTEST` is forced ON: the SDK ships GoogleTest.
- The install rules of the [editor SDK](#editor-sdk) are defined.

`ENGINE_EDITOR` is an engine-repo option. A tree that adds Wind with `add_subdirectory` and sets it fails the configure with a message that points at the SDK. A game builds its editor module in [SDK mode](#sdk-mode).

## Launcher build

`ENGINE_LAUNCHER` (preset `vs-launcher`) adds `launcher/`: `wind_launcher`, an executable through `engine_configure_app` against the static engine, so its cooked `assets/` and `assets/engine/` land beside it, and with tests `wind_launcher_tests` (the state, SDK, and project logic without the UI). `cmake --install build-launcher --config Release --prefix out/launcher` installs `bin/wind_launcher.exe`, its `.pdb` when the configuration makes one, `bin/assets/` (its catalog and assets), and `bin/assets/engine/`. See [Launcher](../features/Launcher.md).

## Version

`project(engine VERSION x.y.z)` in the root `CMakeLists.txt` is the engine version (`PROJECT_VERSION`), semver. The editor SDK records it in `sdk.toml` and `WindConfigVersion.cmake`. A release is a commit on `main` that sets the version, tagged `vx.y.z`, so an SDK of an old version can be built again from its tag. The version is not a build id input: two builds of one version from different commits still get different ids when a public header differs.

`WINDOWS_EXPORT_ALL_SYMBOLS` exports functions, not data. Mutable state a module must share stays in a `.cpp` behind an `ENGINE_API` function. See [Boundaries](../architecture/Boundaries.md).

## Build id

`engine_build_id` runs `cmake/build_id.cmake` before `engine` compiles. It writes `<build>/generated/engine_build_id/<config>/engine/build_id.h`. That directory is a `PUBLIC` include of `engine`, so `#include <engine/build_id.h>` works in the engine, tests, and games.

The header holds `engine::kBuildId` and `engine::kBuildIdCStr`, the same id as a `std::string_view` and as a null-terminated `char` array. The id is the first 16 hex digits of a SHA-256 over

- every public header under `include/` (relative path and content, sorted by path),
- `CMAKE_CXX_COMPILER_ID` and `CMAKE_CXX_COMPILER_VERSION`,
- the configuration,
- `INTERFACE_COMPILE_DEFINITIONS` of `engine` for that configuration (`ENGINE_WITH_WINDOW`, `ENGINE_SHARED`, `ENGINE_EDITOR`, `ENGINE_UI_PROFILER`, `ENGINE_CLI_SERVER`, the web, Android, and GLES flags).

Each configuration has its own header, so Debug and Release ids differ under Visual Studio. The step depends on every public header and reruns when one changes. The header is rewritten only when the id changes.

The header also defines `ENGINE_BUILD_DEBUG_CRT` (1 when that configuration links the MSVC debug CRT, which is Debug; 0 otherwise and off MSVC) and `ENGINE_BUILD_ITERATOR_DEBUG_LEVEL` (2 with the debug CRT, else 0). `cmake/build_id.cmake` gets the first as `-DDEBUG_CRT=$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>`. They are not hash inputs: the configuration already is.

`engine::build_id()` (`include/engine/core/build_info.h`, `src/core/build_info.cpp`) returns `kBuildId` as compiled into `engine`. A game module returns `kBuildIdCStr` from `wind_game_build_id`, so the id is the one the game was compiled against. In the editor build the two are compared.

## What links

Always, PRIVATE: spdlog, tinyxml2, tomlplusplus. PUBLIC: glm.

Windows also links `ws2_32`, `advapi32`, and `winhttp` PUBLIC, because `cli_server.cpp` uses WinSock and ACL APIs, `src/net/winhttp_session.cpp` uses WinHTTP, and a static library does not propagate PRIVATE system libs. The shared editor build keeps the same rule.

`ENGINE_WITH_WINDOW` adds SDL3 (static, except Android where SDL is shared), nanovg, and either glad plus `OpenGL::GL` or, with GLES, SDL and nanovg (Android also GLESv3, EGL, `android`, `log`).

`ENGINE_WITH_AUDIO` adds SDL3 if it is not already there, then SDL3_mixer with every format except WAVE turned OFF. `SDLMIXER_VENDORED` is OFF.

`engine_add_sdl3` on Emscripten forces `SDL_EMSCRIPTEN_PERSISTENT_PATH` to `/storage` so `user_data_directory` is not an in-memory `/libsdl`.

## Game functions

`cmake/wind_game.cmake` defines `engine_prepare_runtime`, `engine_configure_app`, `engine_add_game`, `engine_add_web_game`, and `engine_add_android_game`. The root `CMakeLists.txt` includes it in the source build. The SDK installs the same file, and `WindConfig.cmake` includes it in SDK mode.

The functions run in the calling directory's scope, so what they need from the engine is in cache variables both contexts set:

| Variable | Source build | SDK mode |
| --- | --- | --- |
| `ENGINE_FROM_SDK` | OFF | ON |
| `ENGINE_BUILTIN_ASSETS_DIR` | `builtin_assets/` | `<sdk>/bin/assets/engine` |
| `ENGINE_COOKED_CATALOG` | `<build>/generated/engine/catalog.toml`, cooked by the target `engine_builtin_catalog` | `<sdk>/bin/assets/engine/catalog.toml`, already cooked |
| `ENGINE_CMAKE_DIR` | engine source tree | not set (only the web link step reads it) |

`engine`, `asset_codegen`, and `icon_codegen` are targets in both: built in the source build, imported in SDK mode.

## `engine_add_game`

Call it after `find_package(Wind)` (SDK mode) or `add_subdirectory` of the engine source (static build). At least one source file. In the source build `ENGINE_WITH_WINDOW` must be ON (the subdirectory default). It creates the target (table below) and hands it to `engine_configure_app`.

| Platform | Target |
| --- | --- |
| Desktop | `add_executable` |
| Desktop, `ENGINE_EDITOR` or SDK mode | `add_library` SHARED, `PRIVATE ENGINE_GAME_MODULE=1`, no `lib` prefix |
| Android | `add_library` SHARED, `OUTPUT_NAME` `main`, plus `--defsym=SDL_main=main` and `SDL3::SDL3main` when that target exists |
| Apple | `MACOSX_BUNDLE` ON |

`engine_configure_app(target)` is everything after the target exists. The editor calls it for `wind_editor` too. It reads `include/`, `assets/`, and `icon.png` from the calling directory. C++23, no extensions. MSVC: `/W4 /permissive- /utf-8`. MSVC Windows executable, every config except Debug: `/SUBSYSTEM:WINDOWS` and `/ENTRY:mainCRTStartup` so there is no console and `main` stays the entry.

The game's source calls `ENGINE_GAME(GameClass)` from `<engine/game_entry.h>` instead of writing `main`. The executable gets `main`. The module gets the exports the editor resolves. See [Core](../modules/Core.md).

### Game module (`ENGINE_EDITOR` or SDK mode)

- The `.dll` lands where the executable would: `RUNTIME_OUTPUT_DIRECTORY` `bin/` (a `.so` uses `LIBRARY_OUTPUT_DIRECTORY` `bin/`). The import library goes to `lib/`. The `.pdb` sits beside the `.dll`.
- In SDK mode with MSVC: `MSVC_RUNTIME_LIBRARY` is `MultiThreadedDLL` (`MultiThreadedDebugDLL` against a Debug SDK), and against a Release SDK the game's Release configuration adds `/Zi` and `/DEBUG /OPT:REF /OPT:ICF`, so the module has a `.pdb` in both configurations. `DebugGame` flags come from [SDK mode](#configurations).
- MSVC links with `/PDBALTPATH:<name>.pdb` (`$<TARGET_PDB_FILE_NAME>`), the same as `/PDBALTPATH:%_PDB%`. The module records only the PDB file name, so a debugger finds the PDB beside a copy of the module. The literal `%_PDB%` is not used because the Visual Studio generator escapes `%` in link options.
- No `/SUBSYSTEM` or `/ENTRY` flags. The icon `.rc` is still compiled in. It is harmless in a `.dll`.
- `file(GENERATE)` writes `<build>/wind/<target>.<config>.module` per configuration: one line, the module's absolute path (`$<TARGET_FILE>`). It exists once CMake generated, before any build. The editor reads it after a build to know which file to load ([Editor](../features/Editor.md#build)); it does not guess `bin/<config>/`.
- Asset codegen and `engine_prepare_runtime` run as for the executable, so `bin/assets/catalog.toml` and the game's assets sit beside the module. In the source build `bin/assets/engine/` and `engine.dll` are copied there too. In SDK mode they are not: the editor that loads the module has its own `engine.dll` and `assets/engine/`, and Play reads only `<module dir>/assets/`.

The game links `engine` PRIVATE. If the game has an `include/` directory it is PRIVATE too. `engine/src` is not on the game's include path.

Asset and icon steps: [Pipeline](Pipeline.md). Web adds `cmake/web/link_flags.cmake` (`engine_target_web_link_options`, which also links `-sFETCH=1` for [Net](../modules/Net.md), and `engine_target_web_preload`) and copies `favicon.png` beside the output when icons were generated.

`engine_add_web_game` and `engine_add_android_game` call `engine_add_game` and fail the configure when `EMSCRIPTEN` or `ANDROID` is not set.

## Runtime assets subdirectory

`engine_prepare_runtime` copies a target's own `assets/` tree and cooked `catalog.toml` to `<output>/assets/`. The target property `ENGINE_RUNTIME_ASSETS_DIR` moves both to `<output>/assets/<dir>/`. `wind_editor` sets it to `editor`, so the editor and a game module built into the same `bin/` each keep their own `catalog.toml`. The engine's own assets stay in `assets/engine/` either way.

## Editor targets

`editor/CMakeLists.txt`, added when `ENGINE_EDITOR` is ON.

| Target | What |
| --- | --- |
| `wind_editor` | `add_executable` from `editor/src/`, then `engine_configure_app`. Its `assets/` cook to `asset_ids.h` and land in `bin/assets/editor/` |
| `wind_editor_tests` | With `ENGINE_BUILD_TESTS`. `editor/tests/play_session_test.cpp` and `editor/src/play_session.cpp`, linked with `engine` and `GTest::gtest_main`, `tests/` on the include path. `gtest_discover_tests`, so `ctest` runs it |

With `ENGINE_BUILD_TESTS` the root also builds three fixture modules from `tests/fixtures/game_module/fixture_game.cpp` into `test_fixtures/`: `wind_fixture_game_ok`, `wind_fixture_game_wrong_build_id` (`FIXTURE_WRONG_BUILD_ID`), and `wind_fixture_game_no_destroy` (`FIXTURE_NO_DESTROY`). They link `engine` and use `/PDBALTPATH` like a game module. `engine_tests` and `wind_editor_tests` depend on them and get their paths as `WIND_FIXTURE_GAME`, `WIND_FIXTURE_GAME_WRONG_BUILD_ID`, and `WIND_FIXTURE_GAME_NO_DESTROY` (`$<TARGET_FILE:...>`).

## Editor SDK

With `ENGINE_EDITOR` the root defines install rules. `cmake --install build-editor --config Release --prefix out/sdk` (`/out/` is gitignored) writes one configuration:

```
out/sdk/
  bin/
    wind_editor.exe  engine.dll  asset_codegen.exe  icon_codegen.exe  (+ their .pdb when the config makes them)
    assets/engine/   builtin_assets/ and the cooked catalog.toml
    assets/editor/   editor/assets/ and the editor's cooked catalog.toml
  lib/engine.lib
  include/
    engine/**        public headers and the generated engine/build_id.h of that configuration
    glm/**           glm headers (*.h, *.hpp, *.inl)
    gtest/**         GoogleTest headers
  lib/gtest.lib  gtest_main.lib  (+ gtest.pdb, gtest_main.pdb on MSVC)
  cmake/
    WindConfig.cmake         imported targets, game functions, configuration check
    WindConfigVersion.cmake  accepts only this engine version
    wind_game.cmake          game functions
  source/          the engine's build input (see below)
  sdk.toml         version, commit, dirty, config, build_id
```

The `.pdb` rules are `OPTIONAL` (a configuration without symbols would have none), but every configuration of the editor build makes them: Debug and RelWithDebInfo by default, Release through `/Zi` and `/DEBUG`. So `bin/` holds `engine.pdb`, `wind_editor.pdb`, `asset_codegen.pdb`, and `icon_codegen.pdb`. The binaries need only system DLLs and the VC++ runtime (SDL3, SDL3_mixer, glad, nanovg, spdlog, tinyxml2, and tomlplusplus are static inside `engine.dll`). tinyxml2 is added `EXCLUDE_FROM_ALL` so its own install rules stay out of the SDK.

GoogleTest is the editor build's own `gtest` and `gtest_main` (shared CRT, the SDK's configuration), so a game's tests need no submodule. Built with `/Zi`, their objects point at a compile `.pdb`; the SDK puts `gtest.pdb` and `gtest_main.pdb` beside the libraries, where the linker of a game's test looks for them, or every `/DEBUG` link warns LNK4099. The Debug compile `.pdb` is renamed from googletest's `gtestpdb_debug_postfix-NOTFOUND.pdb` (its name when `CMAKE_DEBUG_POSTFIX` is unset) to `gtest.pdb`.

`source/` holds `CMakeLists.txt`, `cmake/`, `include/`, `src/`, `tools/`, `builtin_assets/`, and `external/` without `.git` and `.github`, about 120 MB. The editor and SDK mode do not use it. It builds the static engine of exactly this version: `add_subdirectory(<sdk>/source wind)` in a game's own configure. That is the input of the future export, and until then the way to build a standalone executable by hand ([Game Consumer](Game%20Consumer.md#standalone-executable)). Those files are installed without a line of output each (`CMAKE_INSTALL_MESSAGE NEVER`).

`sdk.toml` is written last, by `cmake/sdk_manifest.cmake` from an `install(CODE)` step:

```toml
version = "0.1.0"                                   # project(engine VERSION)
commit = "50c292f95fec1d06617eeb17409cee2b612691e2" # git rev-parse HEAD of the engine checkout
dirty = false                                       # git status --porcelain was not empty
config = "Release"
build_id = "5266d5a5ebe5603e"                       # kBuildId of the installed configuration
```

It is how the launcher and the editor know an SDK ([Editor Plan](../architecture/Editor%20Plan.md#next-projects-launcher-build-on-play)); its keys are only added, never renamed. An SDK with `dirty = true` is not the build of its version's tag. The install fails without git.

`WindConfig.cmake` is generated per configuration from `cmake/WindConfig.cmake.in` with `file(GENERATE)`, so it holds the evaluated values of the installed configuration, not generator expressions a game would evaluate against its own configuration. It finds the SDK root from its own location (the SDK can move), sets the cache entries of [Game functions](#game-functions) plus `WIND_SDK_DIR` and `WIND_SDK_CONFIG` (for example `Release`), checks that the files it imports exist, and defines:

| Target | What |
| --- | --- |
| `engine` (`engine::engine`) | `SHARED IMPORTED GLOBAL`. `IMPORTED_LOCATION` `bin/engine.dll` and `IMPORTED_IMPLIB` `lib/engine.lib`, without a configuration suffix, so every game configuration uses them. `INTERFACE_INCLUDE_DIRECTORIES` `include/`. `INTERFACE_COMPILE_DEFINITIONS` the SDK's `PUBLIC` defines for its configuration: `ENGINE_SHARED=1`, `ENGINE_EDITOR=1`, `ENGINE_UI_PROFILER`, `ENGINE_CLI_SERVER`, `ENGINE_WITH_WINDOW=1` (the same in every configuration of the editor build). `cxx_std_23`. Links `glm::glm`, and `ws2_32` and `advapi32` on Windows |
| `glm::glm` | `INTERFACE IMPORTED GLOBAL` on `include/` |
| `asset_codegen`, `icon_codegen` | `IMPORTED GLOBAL` executables in `bin/`. They load `engine.dll` from beside themselves |
| `GTest::gtest`, `GTest::gtest_main` | `STATIC IMPORTED GLOBAL` on `lib/`, includes `include/`; `gtest_main` links `gtest` |

Then it includes CMake's `GoogleTest` module (`gtest_discover_tests`) and `wind_game.cmake`, prints `Wind <version>: <config> SDK at <dir>`, and calls `engine_sdk_configurations()` ([Configurations](#configurations)). `WindConfigVersion.cmake` comes from `write_basic_package_version_file` with `ExactVersion`, so `find_package(Wind 0.2.0)` refuses a 0.1.0 SDK and lists it.

## SDK mode

A game's `CMakeLists.txt` calls `find_package(Wind REQUIRED)` after its `project()`, then `engine_add_game`. CMake finds `<sdk>/cmake/WindConfig.cmake` through `CMAKE_PREFIX_PATH=<sdk>` (or `Wind_DIR=<sdk>/cmake`): the editor will pass it when it configures a project; until then the game's `CMakeUserPresets.json` sets it ([Game Consumer](Game%20Consumer.md#editor-module)). The game repo has no engine submodule.

No engine source, host tool, editor, fixture module, or `engine_tests` is configured. `engine_add_game` builds the module only, into `bin/<config>/` with `assets/catalog.toml` and the game's assets. The headers, `build_id.h`, and `engine.lib` are the SDK's, so the module's `kBuildId` is the SDK's by construction.

### Configurations

A module shares the C runtime, the heap, and STL objects with the SDK's `engine.dll`, so its CRT must match the SDK's. `engine_sdk_configurations()` (`cmake/wind_game.cmake`) checks the game's configurations and fails the configure with the fix when they do not fit:

| SDK (`WIND_SDK_CONFIG`) | Game configurations | CRT |
| --- | --- | --- |
| Release (any configuration but Debug) | `DebugGame` and `Release` | `/MD` in both |
| Debug | `Debug` | `/MDd` |

The list is `CMAKE_CONFIGURATION_TYPES` (Visual Studio, Ninja Multi-Config) or `CMAKE_BUILD_TYPE` (Ninja, Makefiles). A subset (only `Release`) is fine. It has to be set before the game's `project()`, which runs before `find_package(Wind)`, so the game's `editor` preset sets it (`"CMAKE_CONFIGURATION_TYPES": "DebugGame;Release"`, see [Game Consumer](Game%20Consumer.md#editor-module)); the engine only checks it. CMake's default list (`Debug;Release;MinSizeRel;RelWithDebInfo`) fails: Debug would load the debug CRT into a release `engine.dll`.

| | `DebugGame` | `Release` |
| --- | --- | --- |
| Compile (MSVC) | `/MD /Od /Ob0 /Zi /RTC1`, no `NDEBUG`, no `_DEBUG` | CMake's `/O2 /Ob2 /DNDEBUG`, plus `/Zi` on the module |
| Link (MSVC) | `/DEBUG /INCREMENTAL` | CMake's, plus `/DEBUG /OPT:REF /OPT:ICF` on the module |
| `_ITERATOR_DEBUG_LEVEL` | 0 | 0 |
| Other compilers | `-O0 -g` | CMake's |

CMake has no flags for a configuration it does not know, so `engine_sdk_configurations` sets `CMAKE_C_FLAGS_DEBUGGAME`, `CMAKE_CXX_FLAGS_DEBUGGAME`, `CMAKE_EXE_LINKER_FLAGS_DEBUGGAME`, `CMAKE_SHARED_LINKER_FLAGS_DEBUGGAME`, `CMAKE_MODULE_LINKER_FLAGS_DEBUGGAME`, `CMAKE_STATIC_LINKER_FLAGS_DEBUGGAME`, and `CMAKE_MSVC_RUNTIME_LIBRARY` (`MultiThreadedDLL`) as cache defaults. Cache entries reach every directory of the game's tree, so the module and the game's own test executables all build `DebugGame` with the release CRT. The `/MD` in the flags covers targets whose directory has policy CMP0091 OLD and so reads the runtime from the flags. A game can override the defaults in its preset. `DebugGame` is not in `DEBUG_CONFIGURATIONS`, so `debug`/`optimized` link items pick the release side. The `Release` extras are per target in `engine_add_game`, because `CMAKE_CXX_FLAGS_RELEASE` already exists in the game's cache.

Game asserts and `#if !defined(NDEBUG)` work in `DebugGame`. STL checks and the CRT debug heap need `/MDd`, so they need a Debug SDK.

A Debug SDK (`cmake --build build-editor --config Debug`, then `cmake --install build-editor --config Debug --prefix out/sdk-debug`) is for debugging the engine itself: `WIND_SDK_CONFIG` is Debug, the game uses one configuration, `Debug`, with CMake's Debug flags, and the engine is unoptimized too. `engine_tests` in Debug stays the main tool for engine work.

### CRT guard

Under `ENGINE_GAME_MODULE` on MSVC, `<engine/game_entry.h>` compares the module's `_DEBUG` and `_ITERATOR_DEBUG_LEVEL` with `ENGINE_BUILD_DEBUG_CRT` and `ENGINE_BUILD_ITERATOR_DEBUG_LEVEL` from the SDK's `<engine/build_id.h>` and stops the compile with `#error` on a mismatch ("Build it in DebugGame or Release against a Release SDK", or "Build it in Debug" against a Debug SDK). A wrong CRT is a compile error, not a heap corruption on Play. The guard sits in the header of `ENGINE_GAME`, so a module that writes the three exports by hand (the test fixtures) is not checked.

## Tests

`engine_tests` globs `tests/*_test.cpp`, links `engine` and `GTest::gtest_main`, and adds `src/` as a PRIVATE include so a test can reach a private header. With the window flag it also links SDL3 and, unless GLES, glad.

Compile definitions: `ENGINE_BUILTIN_ASSETS_DIR`, `ENGINE_SOURCE_DIR`.

`engine_prepare_runtime(engine_tests)` copies builtin assets beside the test binary, and `engine.dll` in the editor build. Desktop uses `gtest_discover_tests` with `DISCOVERY_MODE PRE_TEST`.

## Host tools

Not cross-compiling: `asset_codegen`, `asset_guid` (link `engine`), `wind-cli` (does not link `engine`; WinSock on Windows), `icon_codegen` (links `engine` and adds `src/` so it can include `resources/icon_codegen.h`).

Cross-compiling: `asset_codegen` and `icon_codegen` are `IMPORTED` from the host-path cache variables. `asset_guid` and `wind-cli` are not built in that configure.

`tests/cmake_sanity_test.cpp` also checks the CMake text: the icon, favicon, and Android manifest rules in `cmake/wind_game.cmake`, and that the root has no SDK branch, has a version, installs `WindConfig.cmake` with an exact-version file, `sdk.toml`, and `source/`, and that the config imports `engine`, the tools, and GoogleTest and sets up `DebugGame`. It also checks that the editor build has the profiler and the CLI, and that `ENGINE_BUILD_DEBUG_CRT` and `ENGINE_BUILD_ITERATOR_DEBUG_LEVEL` describe the test binary's own CRT.

## See also

- [Game Consumer](Game%20Consumer.md)
- [Boundaries](../architecture/Boundaries.md)
