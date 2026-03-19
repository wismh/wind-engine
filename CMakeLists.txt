cmake_minimum_required(VERSION 3.20)
# The engine version, `<major>.<minor>.<patch><stage>`: 0.1.7a is alpha, 0.1.7b beta, 0.1.7 production. The numbers and
# the stage are set here; a person raises the minor or the stage by editing them. The patch comes from the release
# build (-DENGINE_VERSION_PATCH=N, .github/workflows/release.yml); without it the build is local and its version is
# `<major>.<minor>.0<stage>-dev`. An editor SDK records the full version in sdk.toml (`version`), and a release is tagged
# v<full version>. CMake's own project version stays the three numbers (WindConfigVersion.cmake compares only those, a
# game's find_package(Wind) gets that exact number). docs/tech/build/Release.md, docs/tech/build/CMake.md
set(ENGINE_VERSION_MAJOR 0)
set(ENGINE_VERSION_MINOR 1)
set(ENGINE_VERSION_STAGE "a")
set(ENGINE_VERSION_PATCH "" CACHE STRING "Patch number of a release build (empty: a local build, version ...-dev)")
if(NOT ENGINE_VERSION_STAGE MATCHES "^(a|b)?$")
    message(FATAL_ERROR "ENGINE_VERSION_STAGE is \"${ENGINE_VERSION_STAGE}\"; it is a, b, or empty (production).")
endif()
if(ENGINE_VERSION_PATCH STREQUAL "")
    set(_engine_version_patch 0)
    set(_engine_version_dev "-dev")
elseif(ENGINE_VERSION_PATCH MATCHES "^(0|[1-9][0-9]*)$")
    set(_engine_version_patch ${ENGINE_VERSION_PATCH})
    set(_engine_version_dev "")
else()
    message(FATAL_ERROR "ENGINE_VERSION_PATCH is \"${ENGINE_VERSION_PATCH}\"; it is a whole number without leading zeros.")
endif()
set(ENGINE_VERSION_NUMBER "${ENGINE_VERSION_MAJOR}.${ENGINE_VERSION_MINOR}.${_engine_version_patch}")
set(ENGINE_VERSION_FULL "${ENGINE_VERSION_NUMBER}${ENGINE_VERSION_STAGE}${_engine_version_dev}")
project(engine VERSION ${ENGINE_VERSION_NUMBER} LANGUAGES CXX C)
message(STATUS "Wind engine ${ENGINE_VERSION_FULL}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_engine_is_root ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
    set(_engine_is_root OFF)
endif()

option(ENGINE_BUILD_TESTS "Build engine_tests (GoogleTest)" ${_engine_is_root})
# Split out from ENGINE_BUILD_TESTS so a game can vendor GoogleTest for its own test target
# without also compiling the engine's own engine_tests suite (see engine_add_game usage below).
option(ENGINE_WITH_GTEST "Vendor GoogleTest (implied by ENGINE_BUILD_TESTS)" ${ENGINE_BUILD_TESTS})
# Root builds stay headless (`engine_tests`). A game `add_subdirectory` turns the window on.
if(_engine_is_root)
    option(ENGINE_WITH_WINDOW "SDL3 + OpenGL + NanoVG backend" OFF)
else()
    option(ENGINE_WITH_WINDOW "SDL3 + OpenGL + NanoVG backend" ON)
endif()
# Same split as ENGINE_WITH_WINDOW: root stays silent (`engine_tests` never opens a mixer
# device), a game gets sound without having to FORCE it on itself.
if(_engine_is_root)
    option(ENGINE_WITH_AUDIO "SDL3_mixer audio backend" OFF)
else()
    option(ENGINE_WITH_AUDIO "SDL3_mixer audio backend" ON)
endif()
set(ENGINE_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Wind engine source tree")

function(engine_require_submodule dir)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/${dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "Missing submodule external/${dir}. From the engine repo run:\n"
            "  git submodule update --init --recursive\n"
            "See external/README.md")
    endif()
endfunction()

function(engine_require_file rel)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${rel}")
        message(FATAL_ERROR
            "Missing ${rel} (git submodule not cloned?). From the engine repo run:\n"
            "  git submodule update --init --recursive\n"
            "WINDOW deps: external/SDL3, external/glad, external/nanovg.\n"
            "AUDIO deps: external/SDL3, external/SDL_mixer.\n"
            "See external/README.md")
    endif()
endfunction()

set(ENGINE_FROM_SDK OFF CACHE INTERNAL "Wind engine imported from an installed SDK (find_package(Wind))")

if(EMSCRIPTEN)
    set(_engine_web_default ON)
else()
    set(_engine_web_default OFF)
endif()
option(ENGINE_WITH_WEB "WebGL2 / Emscripten profile (shader adapt, RAF, packaged assets)" ${_engine_web_default})
if(ANDROID)
    set(_engine_android_default ON)
else()
    set(_engine_android_default OFF)
endif()
option(ENGINE_WITH_ANDROID "Android NDK / GLES / APK profile" ${_engine_android_default})
if(EMSCRIPTEN OR ANDROID)
    set(_engine_gles_default ON)
else()
    set(_engine_gles_default OFF)
endif()
option(ENGINE_WITH_GLES "OpenGL ES 3.0 backend (no glad, NanoVG GLES3, shader adapt)" ${_engine_gles_default})
# Editor build: engine becomes a shared library so the editor and a game module loaded into it share
# one engine. Exported games (this OFF) link engine statically. Desktop only.
option(ENGINE_EDITOR "Editor build: shared engine library (engine.dll)" OFF)
if(ENGINE_EDITOR)
    if(NOT _engine_is_root)
        message(FATAL_ERROR
            "ENGINE_EDITOR is not a game option. A game builds its editor module against an installed editor "
            "SDK with find_package(Wind) and CMAKE_PREFIX_PATH=<sdk>. Make the SDK in the engine repo with the "
            "vs-editor preset and cmake --install. See README.md and docs/tech/build/Game Consumer.md")
    endif()
    if(EMSCRIPTEN OR ANDROID)
        message(FATAL_ERROR
            "ENGINE_EDITOR is desktop only. Web and Android builds stay on the static engine; "
            "configure them with ENGINE_EDITOR=OFF.")
    endif()
    if(NOT ENGINE_WITH_WINDOW)
        message(FATAL_ERROR
            "ENGINE_EDITOR needs ENGINE_WITH_WINDOW=ON (the editor opens a window). "
            "Use the vs-editor preset.")
    endif()
    # Every static library linked into the shared engine (spdlog, tinyxml2, glad, nanovg, SDL3,
    # SDL3_mixer) must be position independent off Windows. Set before the submodules are added.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    # The installed SDK is relocatable: wind_editor and the host tools find libengine beside themselves. (Windows
    # looks for engine.dll in the executable's directory without help.) The build tree keeps CMake's absolute
    # rpath, so it runs from where it was built.
    if(APPLE)
        set(CMAKE_INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set(CMAKE_INSTALL_RPATH "$ORIGIN")
    endif()
    # A game module shares the C++ runtime with engine.dll, so the CRT is fixed here (the CMake default, made
    # explicit) and recorded in <engine/build_id.h> for the guard in <engine/game_entry.h>.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    if(MSVC)
        # Release with symbols: the same /O2 /Ob2 code, plus a .pdb for every binary (engine.dll, wind_editor,
        # the host tools, the libraries inside engine.dll) for call stacks, breakpoints, and crash dumps. The
        # SDK installs them. /DEBUG turns /OPT:REF and /OPT:ICF off unless they are given.
        add_compile_options("$<$<CONFIG:Release>:/Zi>")
        add_link_options(
            "$<$<CONFIG:Release>:/DEBUG>"
            "$<$<CONFIG:Release>:/OPT:REF>"
            "$<$<CONFIG:Release>:/OPT:ICF>")
    endif()
    set(_engine_library_kind SHARED)
else()
    set(_engine_library_kind STATIC)
endif()
# UI bench (bench/): renders one UI scene in one mode, records the UI profiler for a fixed number of frames, writes the
# snapshot JSON, and exits. The baseline of docs/tech/architecture/UI Performance Plan.md; preset vs-bench, measured in
# RelWithDebInfo. docs/tech/features/UI Bench.md
option(ENGINE_BENCH "Build wind_ui_bench (static engine, desktop window)" OFF)
if(ENGINE_BENCH)
    if(NOT _engine_is_root OR ENGINE_EDITOR OR EMSCRIPTEN OR ANDROID OR NOT ENGINE_WITH_WINDOW)
        message(FATAL_ERROR
            "ENGINE_BENCH builds wind_ui_bench in the engine repo against the static desktop engine: it needs "
            "ENGINE_WITH_WINDOW=ON and ENGINE_EDITOR=OFF, and no Emscripten or Android. Use the vs-bench preset.")
    endif()
endif()
# Player build: the prebuilt static engine the editor SDK ships in <sdk>/player/<flavour>/, so an Export compiles the
# game and links instead of compiling the engine. Configured by cmake/player_build.cmake, never by hand. Two flavours:
#   Development  optimized, with symbols, with the UI profiler and the CLI server (the game's DebugGame configuration)
#   Shipping     optimized, without symbols, profiler, or CLI server (the game's Release configuration)
# Both link the release DLL CRT, have the window and the mixer, and are not the editor build. docs/tech/build/CMake.md
set(ENGINE_PLAYER "" CACHE STRING "Player build of the static engine for the editor SDK: Development, Shipping, or empty")
set_property(CACHE ENGINE_PLAYER PROPERTY STRINGS "" Development Shipping)
if(ENGINE_PLAYER)
    if(NOT ENGINE_PLAYER STREQUAL "Development" AND NOT ENGINE_PLAYER STREQUAL "Shipping")
        message(FATAL_ERROR "ENGINE_PLAYER is \"${ENGINE_PLAYER}\"; it is Development or Shipping.")
    endif()
    if(NOT _engine_is_root OR ENGINE_EDITOR OR EMSCRIPTEN OR ANDROID OR NOT ENGINE_WITH_WINDOW OR NOT ENGINE_WITH_AUDIO
            OR ENGINE_BUILD_TESTS OR ENGINE_BENCH)
        message(FATAL_ERROR
            "ENGINE_PLAYER builds the desktop player libraries of the editor SDK: engine repo root, ENGINE_WITH_WINDOW=ON, "
            "ENGINE_WITH_AUDIO=ON, and no ENGINE_EDITOR, tests, bench, Emscripten, or Android. "
            "cmake/player_build.cmake configures it; see docs/tech/build/CMake.md")
    endif()
    # The game executable links the release DLL CRT, like the SDK's engine.dll and game modules.
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if(ENGINE_PLAYER STREQUAL "Development")
        # Debug information inside the object files (/Z7), so the static libraries carry it into the game's own .pdb
        # and no compile .pdb has to travel with them.
        add_compile_options("$<IF:$<BOOL:${MSVC}>,/Z7,-g>")
    endif()
endif()
set(ENGINE_WEB_SHELL "" CACHE FILEPATH "Optional HTML shell for Emscripten (defaults to cmake/web/shell.html)")
set(ENGINE_HOST_ASSET_CODEGEN "" CACHE FILEPATH "Native asset_codegen when cross-compiling (Emscripten or Android)")
set(ENGINE_HOST_ICON_CODEGEN "" CACHE FILEPATH "Native icon_codegen when cross-compiling (Emscripten or Android)")
# Gradle passes -DENGINE_ANDROID_ASSETS_OUT=${project.buildDir}/wind-assets and packs that same tree.
set(ENGINE_ANDROID_ASSETS_OUT "" CACHE PATH
    "Cooked APK assets directory shared with Gradle sourceSets (empty: stage beside libmain.so only)")

engine_require_submodule(glm)
engine_require_submodule(tomlplusplus)
engine_require_submodule(tinyxml2)
engine_require_submodule(spdlog)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

set(tomlplusplus_INSTALL OFF CACHE BOOL "" FORCE)
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(external/glm)
add_subdirectory(external/tomlplusplus)
# EXCLUDE_FROM_ALL keeps tinyxml2's own install rules out of the editor SDK; engine still builds it.
add_subdirectory(external/tinyxml2 EXCLUDE_FROM_ALL)
add_subdirectory(external/spdlog)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "[/\\\\]render[/\\\\]opengl[/\\\\]")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "engine_runtime\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "engine_host\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "engine_instantiate\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "sdl_fatal_error\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "game_module\\.cpp$")

add_library(engine ${_engine_library_kind} ${ENGINE_SOURCES})
add_library(engine::engine ALIAS engine)

if(ENGINE_EDITOR)
    # WINDOWS_EXPORT_ALL_SYMBOLS exports functions, not data. Mutable state reachable from a public
    # header lives in a .cpp behind ENGINE_API (<engine/core/export.h>). ENGINE_SHARED tells every
    # consumer to import; ENGINE_BUILDING tells the engine's own sources to export.
    set_target_properties(engine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    # ENGINE_EDITOR reaches wind_editor, the tests, the fixture modules, and through the SDK every game module.
    # Game code may test it only for tools; gameplay must not depend on it (Principles).
    target_compile_definitions(engine
        PUBLIC ENGINE_SHARED=1 ENGINE_EDITOR=1
        PRIVATE ENGINE_BUILDING=1)
endif()

# <engine/build_id.h>: kBuildId, a hash of the public headers, the compiler, the configuration, and
# the PUBLIC compile definitions (cmake/build_id.cmake). One header per configuration, so a
# multi-config generator (Visual Studio) gives Debug and Release different ids. The definitions are
# read from engine's INTERFACE_COMPILE_DEFINITIONS per configuration at build time, so defines
# added further down this file are covered too.
file(GLOB_RECURSE _engine_public_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.inl")
set(_engine_build_id_config "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,NoConfig>")
set(_engine_build_id_root "${CMAKE_CURRENT_BINARY_DIR}/generated/engine_build_id")
set(_engine_build_id_dir "${_engine_build_id_root}/${_engine_build_id_config}")
add_custom_command(
    OUTPUT "${_engine_build_id_dir}/build_id.stamp"
    BYPRODUCTS "${_engine_build_id_dir}/engine/build_id.h"
    COMMAND ${CMAKE_COMMAND}
        "-DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include"
        "-DOUT=${_engine_build_id_dir}/engine/build_id.h"
        "-DSTAMP=${_engine_build_id_dir}/build_id.stamp"
        "-DCOMPILER=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        "-DCONFIG=${_engine_build_id_config}"
        "-DDEFINES=$<JOIN:$<TARGET_PROPERTY:engine,INTERFACE_COMPILE_DEFINITIONS>,|>"
        "-DDEBUG_CRT=$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_id.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_id.cmake"
        ${_engine_public_headers}
    COMMENT "Engine build id"
    VERBATIM)
add_custom_target(engine_build_id DEPENDS "${_engine_build_id_dir}/build_id.stamp")
add_dependencies(engine engine_build_id)

# The UI profiler and the CLI server: Debug, RelWithDebInfo, DebugGame (a fallback Export built from <sdk>/source), and
# every configuration of the editor build, so the Release editor has the Profiler tab and wind-cli. An exported game's
# Release and MinSizeRel compile the profiler call sites out. The player libraries have it by flavour: Development
# yes, Shipping no. The CLI server is a separate switch, desktop only.
if(ENGINE_EDITOR OR ENGINE_PLAYER STREQUAL "Development")
    set(_engine_tools_config 1)
elseif(ENGINE_PLAYER STREQUAL "Shipping")
    set(_engine_tools_config 0)
else()
    set(_engine_tools_config "$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>,$<CONFIG:DebugGame>>")
endif()
# The debug layout check (src/ui/layout_check.cpp): after every layout sync it lays a copy of the document out from
# scratch and compares the rects, so a change that skipped work it owed is found by the whole UI test suite and the
# bench scenes. Debug configurations always have it; any other build opts in with -DENGINE_UI_LAYOUT_CHECK=ON. Never
# in a Release build by default, and never in the editor or a player.
option(ENGINE_UI_LAYOUT_CHECK "Check every UI layout sync against a layout from scratch (slow; tests and Debug)" OFF)
if(ENGINE_UI_LAYOUT_CHECK AND NOT ENGINE_EDITOR AND NOT ENGINE_PLAYER)
    target_compile_definitions(engine PUBLIC ENGINE_UI_LAYOUT_CHECK)
elseif(NOT ENGINE_EDITOR AND NOT ENGINE_PLAYER)
    target_compile_definitions(engine PUBLIC "$<$<CONFIG:Debug>:ENGINE_UI_LAYOUT_CHECK>")
endif()
target_compile_definitions(engine PUBLIC "$<${_engine_tools_config}:ENGINE_UI_PROFILER>")
# glGetError after every executed draw command, logged once per error and command (src/render/opengl/opengl_backend.cpp).
# Same builds as the profiler: a driver error that drops a draw silently shows up in game.log, and Shipping pays nothing.
target_compile_definitions(engine PRIVATE "$<${_engine_tools_config}:ENGINE_GL_CHECKS>")
if(NOT EMSCRIPTEN AND NOT ANDROID)
    target_compile_definitions(engine PUBLIC "$<${_engine_tools_config}:ENGINE_CLI_SERVER>")
endif()

# BUILD_INTERFACE: the include directories are the source tree's; an installed player (install(EXPORT) below) names none,
# its consumer takes the SDK's include/.
target_include_directories(engine
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${_engine_build_id_dir}>"
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

# glm is part of the public API (IGame, Transform). spdlog/tinyxml2/toml stay PRIVATE. A player build does not
# export glm: its consumer brings glm::glm from the SDK's include/.
target_link_libraries(engine
    PUBLIC
        "$<BUILD_INTERFACE:glm::glm>"
    PRIVATE
        spdlog::spdlog
        tinyxml2::tinyxml2
        tomlplusplus::tomlplusplus)
if(WIN32)
    # cli_server.cpp (ENGINE_CLI_SERVER) calls WinSock and the ACL APIs; src/net/winhttp_session.cpp calls
    # WinHTTP. A static library does not propagate PRIVATE link libs, so games that link engine need them PUBLIC.
    target_link_libraries(engine PUBLIC ws2_32 advapi32 winhttp)
endif()

if(CMAKE_CROSSCOMPILING)
    if(NOT ENGINE_HOST_ASSET_CODEGEN)
        message(FATAL_ERROR
            "Cross-compiling builds need a native asset_codegen (host tool).\n"
            "Configure a native build first, then pass for example:\n"
            "  -DENGINE_HOST_ASSET_CODEGEN=/path/to/build/asset_codegen")
    endif()
    add_executable(asset_codegen IMPORTED GLOBAL)
    set_target_properties(asset_codegen PROPERTIES
        IMPORTED_LOCATION "${ENGINE_HOST_ASSET_CODEGEN}")
else()
    add_executable(asset_codegen "${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_codegen/main.cpp")
    target_link_libraries(asset_codegen PRIVATE engine)

    add_executable(asset_guid "${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_guid/main.cpp")
    target_link_libraries(asset_guid PRIVATE engine)

    # Host client. Does not link engine: it speaks HTTP to a running game or editor, and `launch` starts the
    # wind_editor beside it (the SDK installs both into bin/).
    add_executable(wind-cli "${CMAKE_CURRENT_SOURCE_DIR}/tools/wind_cli/main.cpp")
    if(WIN32)
        target_link_libraries(wind-cli PRIVATE ws2_32)
    endif()
endif()

if(CMAKE_CROSSCOMPILING)
    if(NOT ENGINE_HOST_ICON_CODEGEN)
        message(FATAL_ERROR
            "Cross-compiling builds need a native icon_codegen (host tool).\n"
            "Configure a native build first, then pass for example:\n"
            "  -DENGINE_HOST_ICON_CODEGEN=/path/to/build/icon_codegen")
    endif()
    add_executable(icon_codegen IMPORTED GLOBAL)
    set_target_properties(icon_codegen PROPERTIES
        IMPORTED_LOCATION "${ENGINE_HOST_ICON_CODEGEN}")
else()
    add_executable(icon_codegen "${CMAKE_CURRENT_SOURCE_DIR}/tools/icon_codegen/main.cpp")
    target_link_libraries(icon_codegen PRIVATE engine)
    # icon_codegen_write lives in src/resources (private, no window/SDL dependency needed) rather
    # than include/engine, so the CLI needs the engine target's private include dir too.
    target_include_directories(icon_codegen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

set(ENGINE_BUILTIN_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/builtin_assets" CACHE INTERNAL
    "Source tree for engine builtin assets")
set(ENGINE_COOKED_CATALOG_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/engine" CACHE INTERNAL
    "Build-time output dir for the engine cooked catalog")
set(ENGINE_COOKED_CATALOG "${ENGINE_COOKED_CATALOG_DIR}/catalog.toml" CACHE INTERNAL
    "Cooked catalog.toml for engine builtins")

file(GLOB_RECURSE _ENGINE_BUILTIN_ASSET_FILES CONFIGURE_DEPENDS
    "${ENGINE_BUILTIN_ASSETS_DIR}/*")
add_custom_command(
    OUTPUT "${ENGINE_COOKED_CATALOG}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_COOKED_CATALOG_DIR}"
    COMMAND asset_codegen "${ENGINE_BUILTIN_ASSETS_DIR}" "${ENGINE_COOKED_CATALOG_DIR}" --engine
    DEPENDS asset_codegen ${_ENGINE_BUILTIN_ASSET_FILES}
    COMMENT "Cook engine builtin catalog"
    VERBATIM)
add_custom_target(engine_builtin_catalog DEPENDS "${ENGINE_COOKED_CATALOG}")

# engine_prepare_runtime, engine_configure_app, engine_add_game and the web / Android variants. The
# installed SDK carries the same file (install rules below).
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/wind_game.cmake")

function(engine_add_sdl3)
    # user_data_directory on web is SDL_GetPrefPath under this mount. SDL links idbfs.js and
    # syncs IndexedDB before main when the path is non-empty. FORCE: an earlier empty cache
    # value would leave saves in MEMFS (/libsdl), which disappears on reload.
    if(EMSCRIPTEN)
        set(SDL_EMSCRIPTEN_PERSISTENT_PATH "/storage" CACHE STRING
            "IDBFS mount for SDL_GetPrefPath; Wind user_data_directory writes here" FORCE)
    endif()
    if(TARGET SDL3::SDL3)
        return()
    endif()
    engine_require_file(external/SDL3/CMakeLists.txt)
    if(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/SDL3)
endfunction()

if(ENGINE_WITH_WINDOW)
    engine_add_sdl3()
    engine_require_file(external/nanovg/src/nanovg.c)

    if(NOT ENGINE_WITH_GLES)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/CMakeLists.txt")
            add_subdirectory(external/glad)
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/glad.c")
            add_library(glad STATIC "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/glad.c")
            target_include_directories(glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include")
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/gl.c")
            add_library(glad STATIC "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/gl.c")
            target_include_directories(glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include")
        else()
            message(FATAL_ERROR
                "Missing glad sources under external/glad.\n"
                "Expected CMakeLists.txt, src/glad.c, or src/gl.c.\n"
                "Add submodule https://github.com/wismh/glad.git (or dav1dde/glad generated output).\n"
                "See external/README.md")
        endif()
    endif()

    add_library(nanovg STATIC "${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src/nanovg.c")
    target_include_directories(nanovg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src")
    if(MSVC)
        target_compile_definitions(nanovg PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    file(GLOB_RECURSE ENGINE_GL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/opengl/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/opengl/*.c")
    target_sources(engine PRIVATE
        ${ENGINE_GL_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine_runtime.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine_host.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine_instantiate.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/game_module.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/sdl_fatal_error.cpp")
    target_compile_definitions(engine PUBLIC ENGINE_WITH_WINDOW=1)
    target_include_directories(engine
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/render/opengl"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src")
    if(ENGINE_WITH_GLES)
        if(ANDROID)
            target_link_libraries(engine PRIVATE SDL3::SDL3 nanovg GLESv3 EGL android log)
        else()
            target_link_libraries(engine PRIVATE SDL3::SDL3 nanovg)
        endif()
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(engine PRIVATE SDL3::SDL3 glad nanovg OpenGL::GL)
    endif()
endif()

if(ENGINE_WITH_WEB)
    target_compile_definitions(engine PUBLIC ENGINE_WITH_WEB=1)
endif()
if(ENGINE_WITH_ANDROID)
    target_compile_definitions(engine PUBLIC ENGINE_WITH_ANDROID=1)
endif()
if(ENGINE_WITH_GLES)
    target_compile_definitions(engine PUBLIC ENGINE_WITH_GLES=1)
endif()

if(ENGINE_WITH_AUDIO)
    engine_add_sdl3()
    engine_require_submodule(SDL_mixer)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
    set(SDLMIXER_AIFF OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_VOC OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_AU OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_FLAC OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_GME OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_MOD OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_MP3 OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_MIDI OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_OPUS OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_VORBIS_STB OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_VORBIS_TREMOR OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_WAVPACK OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/SDL_mixer EXCLUDE_FROM_ALL)
    target_compile_definitions(engine PRIVATE ENGINE_WITH_AUDIO=1)
    target_link_libraries(engine PRIVATE SDL3_mixer::SDL3_mixer SDL3::SDL3)
endif()

# The editor build installs GoogleTest into the SDK for a game's own tests.
if((ENGINE_BUILD_TESTS OR ENGINE_EDITOR) AND NOT ENGINE_WITH_GTEST)
    set(ENGINE_WITH_GTEST ON CACHE BOOL "" FORCE)
endif()

if(ENGINE_WITH_GTEST)
    engine_require_submodule(googletest)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    enable_testing()
    add_subdirectory(external/googletest)
    include(GoogleTest)
endif()

if(ENGINE_BUILD_TESTS)
    file(GLOB ENGINE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")

    add_executable(engine_tests ${ENGINE_TEST_SOURCES})
    target_link_libraries(engine_tests PRIVATE engine GTest::gtest_main)
    target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(ENGINE_WITH_WINDOW)
        # WindowSystem is private to engine (linked PRIVATE, so its glad/SDL3 include
        # dirs don't propagate); tests that reach it via src/render/opengl need them directly.
        target_link_libraries(engine_tests PRIVATE SDL3::SDL3)
        if(NOT ENGINE_WITH_GLES)
            target_link_libraries(engine_tests PRIVATE glad)
        endif()
    endif()
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/builtin_assets" _engine_builtin_assets)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _engine_source_dir)
    target_compile_definitions(engine_tests PRIVATE
        "ENGINE_BUILTIN_ASSETS_DIR=\"${_engine_builtin_assets}\""
        "ENGINE_SOURCE_DIR=\"${_engine_source_dir}\"")
    engine_prepare_runtime(engine_tests)
    if(ENGINE_EDITOR)
        # Game modules for tests/game_module_test.cpp and editor/tests/play_session_test.cpp, built from
        # tests/fixtures/game_module/ like a game module: as is, with another build id, and without
        # wind_destroy_game. They load into a process that already holds engine.dll, so they need no
        # copy of it beside them.
        foreach(_variant IN ITEMS ok wrong_build_id no_destroy)
            set(_fixture wind_fixture_game_${_variant})
            add_library(${_fixture} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/game_module/fixture_game.cpp")
            target_link_libraries(${_fixture} PRIVATE engine)
            set_target_properties(${_fixture} PROPERTIES
                PREFIX ""
                CXX_STANDARD 23
                CXX_STANDARD_REQUIRED ON
                CXX_EXTENSIONS OFF
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test_fixtures"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test_fixtures"
                ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
            if(MSVC)
                target_link_options(${_fixture} PRIVATE "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${_fixture}>")
            endif()
            add_dependencies(engine_tests ${_fixture})
        endforeach()
        target_compile_definitions(wind_fixture_game_wrong_build_id PRIVATE FIXTURE_WRONG_BUILD_ID=1)
        target_compile_definitions(wind_fixture_game_no_destroy PRIVATE FIXTURE_NO_DESTROY=1)
        set(ENGINE_FIXTURE_GAME_DEFINES
            "WIND_FIXTURE_GAME=\"$<TARGET_FILE:wind_fixture_game_ok>\""
            "WIND_FIXTURE_GAME_WRONG_BUILD_ID=\"$<TARGET_FILE:wind_fixture_game_wrong_build_id>\""
            "WIND_FIXTURE_GAME_NO_DESTROY=\"$<TARGET_FILE:wind_fixture_game_no_destroy>\"")
        target_compile_definitions(engine_tests PRIVATE ${ENGINE_FIXTURE_GAME_DEFINES})
    endif()
    if(EMSCRIPTEN)
        include("${ENGINE_CMAKE_DIR}/cmake/web/link_flags.cmake")
        engine_target_web_link_options(engine_tests)
        engine_target_web_preload(engine_tests)
    elseif(ANDROID)
        # Device/emulator GoogleTest is not a merge gate.
    else()
        gtest_discover_tests(engine_tests DISCOVERY_MODE PRE_TEST)
    endif()
endif()

# The UI bench. Nothing to install: it runs from build-bench/bin.
if(ENGINE_BENCH)
    add_subdirectory(bench)
endif()

# The player libraries (ENGINE_PLAYER): engine and every static library it links, exported as imported targets in
# the namespace WindPlayer<Flavour>:: and installed into <prefix>/lib and <prefix>/cmake, with cmake/wind_player.cmake
# beside them. cmake/player_build.cmake runs the configure, the build, and this install for the editor SDK
# (<sdk>/player/<Flavour>/). A target the engine reaches and this project builds is exported with it, so a library SDK
# or the mixer adds later needs no list here. Alias names resolve to the real target. glm, header-only toml++, and the
# system libraries are not libraries to install.
if(ENGINE_PLAYER)
    set(WIND_PLAYER_SDK_BUILD_ID "" CACHE STRING "kBuildId of the editor SDK this player belongs to (cmake/player_build.cmake)")
    function(_engine_player_closure root out)
        set(_found "${root}")
        set(_queue "${root}")
        while(_queue)
            list(POP_FRONT _queue _target)
            foreach(_property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
                get_target_property(_items ${_target} ${_property})
                if(NOT _items)
                    continue()
                endif()
                foreach(_item IN LISTS _items)
                    # $<LINK_ONLY:x> and $<TARGET_NAME:x> name a target; any other expression (BUILD_INTERFACE of glm,
                    # a compile feature) does not.
                    if(_item MATCHES "^\\$<(LINK_ONLY|TARGET_NAME):([^>$]+)>$")
                        set(_item "${CMAKE_MATCH_2}")
                    endif()
                    if(NOT TARGET "${_item}")
                        continue()
                    endif()
                    get_target_property(_real ${_item} ALIASED_TARGET)
                    if(_real)
                        set(_item "${_real}")
                    endif()
                    get_target_property(_imported ${_item} IMPORTED)
                    if(_imported OR _item IN_LIST _found)
                        continue()
                    endif()
                    list(APPEND _found "${_item}")
                    list(APPEND _queue "${_item}")
                endforeach()
            endforeach()
        endwhile()
        set(${out} "${_found}" PARENT_SCOPE)
    endfunction()
    _engine_player_closure(engine _engine_player_targets)
    message(STATUS "Wind player ${ENGINE_PLAYER}: ${_engine_player_targets}")
    # A third-party library names its include directories in the source tree (glad does) or in an install tree
    # (INSTALL_INTERFACE). Only this build's targets use them: the exported targets carry none, because the game's
    # compile needs none of these headers and an imported target with a missing include directory is an error.
    foreach(_target IN LISTS _engine_player_targets)
        get_target_property(_directories ${_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT _directories)
            continue()
        endif()
        set(_wrapped)
        foreach(_directory IN LISTS _directories)
            if(_directory MATCHES "^\\$<INSTALL_INTERFACE:")
                continue()
            elseif(_directory MATCHES "^\\$<")
                list(APPEND _wrapped "${_directory}")
            else()
                list(APPEND _wrapped "$<BUILD_INTERFACE:${_directory}>")
            endif()
        endforeach()
        set_property(TARGET ${_target} PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_wrapped}")
    endforeach()

    install(TARGETS ${_engine_player_targets} EXPORT WindPlayer ARCHIVE DESTINATION lib)
    install(EXPORT WindPlayer NAMESPACE "WindPlayer${ENGINE_PLAYER}::" FILE wind_player_targets.cmake DESTINATION cmake)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" WIND_PLAYER_PLATFORM)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/wind_player.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/wind_player.cmake" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/wind_player.cmake" DESTINATION cmake)
endif()

# The editor host and the SDK. ENGINE_EDITOR is ON only in the engine repo (vs-editor); a game builds its
# module against the installed SDK instead (find_package(Wind)).
if(ENGINE_EDITOR)
    add_subdirectory(editor)

    # cmake --install build-editor --config <cfg> --prefix out/sdk: one configuration of the editor, the
    # shared engine, and the host tools, plus what a game needs to build a module against them. Layout,
    # WindConfig.cmake, and sdk.toml: docs/tech/build/CMake.md.
    set(_engine_tools_runtime wind_editor engine asset_codegen icon_codegen wind-cli)
    install(TARGETS ${_engine_tools_runtime}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
        ARCHIVE DESTINATION lib)
    if(MSVC)
        foreach(_tool IN LISTS _engine_tools_runtime)
            install(FILES "$<TARGET_PDB_FILE:${_tool}>" DESTINATION bin OPTIONAL)
        endforeach()
    endif()
    install(DIRECTORY "${ENGINE_BUILTIN_ASSETS_DIR}/" DESTINATION bin/assets/engine)
    install(FILES "${ENGINE_COOKED_CATALOG}" DESTINATION bin/assets/engine)
    get_property(_wind_editor_catalog TARGET wind_editor PROPERTY ENGINE_GAME_CATALOG)
    get_property(_wind_editor_assets TARGET wind_editor PROPERTY ENGINE_GAME_ASSETS)
    install(DIRECTORY "${_wind_editor_assets}/" DESTINATION bin/assets/editor)
    install(FILES "${_wind_editor_catalog}" DESTINATION bin/assets/editor)

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/engine" DESTINATION include)
    install(FILES "${_engine_build_id_dir}/engine/build_id.h" DESTINATION include/engine)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external/glm/glm" DESTINATION include
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")

    # GoogleTest for a game's own tests, built here with the SDK's CRT. The libraries carry /Zi debug info that
    # points at a compile .pdb (googletest puts it beside the library); it goes beside each installed library, where the linker of a game's test looks for it. Its
    # name drops googletest's Debug postfix, which is "pdb_debug_postfix-NOTFOUND" when CMAKE_DEBUG_POSTFIX is unset.
    set(_engine_gtest_libraries gtest gtest_main)
    install(TARGETS ${_engine_gtest_libraries} ARCHIVE DESTINATION lib)
    if(MSVC)
        foreach(_library IN LISTS _engine_gtest_libraries)
            set_target_properties(${_library} PROPERTIES COMPILE_PDB_NAME_DEBUG ${_library})
            install(FILES "$<TARGET_LINKER_FILE_DIR:${_library}>/${_library}.pdb" DESTINATION lib)
        endforeach()
    endif()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external/googletest/googletest/include/gtest" DESTINATION include)

    # The engine's build input, for building the static engine of this exact version (export, or a standalone
    # executable by hand with add_subdirectory(<sdk>/source)). The editor does not use it.
    # Thousands of files: install them without a line each.
    set(CMAKE_INSTALL_MESSAGE NEVER)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" DESTINATION source)
    foreach(_dir IN ITEMS cmake include src tools builtin_assets)
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}" DESTINATION source)
    endforeach()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external/" DESTINATION source/external
        PATTERN ".git" EXCLUDE
        PATTERN ".github" EXCLUDE)
    unset(CMAKE_INSTALL_MESSAGE)

    # WindConfig.cmake bakes the installed configuration and engine's PUBLIC defines for it, evaluated here
    # per configuration, so a game's own configuration never re-evaluates them. The version file accepts only
    # this exact engine version, the three numbers (CMake cannot compare a stage); wind_version.cmake carries the full
    # version (Wind_VERSION_FULL, 0.1.7a or 0.1.0a-dev) and the release patch.
    set(_engine_sdk_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/sdk")
    file(WRITE "${_engine_sdk_dir}/wind_version.cmake"
        "# The engine version of this Wind editor SDK. Generated by the engine's CMakeLists.txt. Do not edit.
"
        "set(Wind_VERSION_FULL \"${ENGINE_VERSION_FULL}\")
"
        "set(WIND_SDK_VERSION_PATCH \"${ENGINE_VERSION_PATCH}\")
")
    file(GENERATE OUTPUT "${_engine_sdk_dir}/$<CONFIG>/WindConfig.cmake"
        INPUT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WindConfig.cmake.in")
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file("${_engine_sdk_dir}/WindConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY ExactVersion)
    install(FILES
        "${_engine_sdk_dir}/$<CONFIG>/WindConfig.cmake"
        "${_engine_sdk_dir}/WindConfigVersion.cmake"
        "${_engine_sdk_dir}/wind_version.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/wind_game.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/wind_export.cmake"
        DESTINATION cmake)

    # Project templates the launcher (github.com/wismh/wind-launcher) copies for a new project, so a project starts from this version's API.
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates/" DESTINATION templates)

    # The player libraries (<sdk>/player/<flavour>/): the static engine built once for every Export, so an Export
    # compiles the game and links. They come from build trees of their own (ENGINE_PLAYER, cmake/player_build.cmake),
    # configured, built, and installed by `cmake --install` of a Release SDK; the target wind_player builds them
    # without installing. Windows, Linux, and macOS. A Debug SDK has none: it is for the engine's own debugging.
    option(ENGINE_SDK_PLAYER "Build the player libraries (Development, Shipping) into a Release editor SDK" ON)
    if(ENGINE_SDK_PLAYER)
        set(_engine_player_defines
            "-DWIND_PLAYER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWIND_PLAYER_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/player"
            "-DWIND_PLAYER_FLAVOURS=Development,Shipping"
            "-DWIND_PLAYER_VERSION_PATCH=${ENGINE_VERSION_PATCH}"
            "-DWIND_PLAYER_SDK_BUILD_ID_HEADER=${_engine_build_id_dir}/engine/build_id.h"
            "-DWIND_PLAYER_GENERATOR=${CMAKE_GENERATOR}"
            "-DWIND_PLAYER_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DWIND_PLAYER_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DWIND_PLAYER_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DWIND_PLAYER_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DWIND_PLAYER_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DWIND_PLAYER_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
            "-DWIND_PLAYER_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "-DWIND_PLAYER_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
        add_custom_target(wind_player
            COMMAND ${CMAKE_COMMAND} ${_engine_player_defines} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/player_build.cmake"
            COMMENT "Wind player libraries (Development, Shipping)"
            USES_TERMINAL VERBATIM)
        set(_engine_player_code)
        foreach(_define IN LISTS _engine_player_defines)
            string(APPEND _engine_player_code " \"${_define}\"")
        endforeach()
        install(CODE "
            if(NOT \"$<CONFIG>\" STREQUAL \"Debug\")
                execute_process(COMMAND \"${CMAKE_COMMAND}\"${_engine_player_code}
                    \"-DWIND_PLAYER_PREFIX=\${CMAKE_INSTALL_PREFIX}/player\"
                    -P \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/player_build.cmake\"
                    RESULT_VARIABLE _wind_player_result)
                if(NOT _wind_player_result EQUAL 0)
                    message(FATAL_ERROR \"The player libraries of the SDK failed to build (exit code \${_wind_player_result})\")
                endif()
            endif()")
    endif()

    # sdk.toml last: version, commit, dirty, configuration, and build id of what was just installed, and the platform
    # and the player flavours this SDK ships (none for a Debug SDK).
    # The names of the release assets: windows, linux, macos.
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _engine_sdk_platform)
    if(_engine_sdk_platform STREQUAL "darwin")
        set(_engine_sdk_platform "macos")
    endif()
    install(CODE "
        set(WIND_SDK_SOURCE_DIR \"${CMAKE_CURRENT_SOURCE_DIR}\")
        set(WIND_SDK_VERSION \"${ENGINE_VERSION_FULL}\")
        set(WIND_SDK_CONFIG \"$<CONFIG>\")
        set(WIND_SDK_BUILD_ID_HEADER \"${_engine_build_id_dir}/engine/build_id.h\")
        set(WIND_SDK_PLATFORM \"${_engine_sdk_platform}\")
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk_manifest.cmake\")")
endif()
