cmake_minimum_required(VERSION 3.16)
project(engine LANGUAGES CXX C)

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
# Root builds stay headless (`engine_tests`). A game `add_subdirectory` turns the window on.
if(_engine_is_root)
    option(ENGINE_WITH_WINDOW "SDL3 + OpenGL + NanoVG backend" OFF)
else()
    option(ENGINE_WITH_WINDOW "SDL3 + OpenGL + NanoVG backend" ON)
endif()
option(ENGINE_WITH_AUDIO "SDL3_mixer audio backend" OFF)
set(ENGINE_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Wind engine source tree")

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
set(ENGINE_WEB_SHELL "" CACHE FILEPATH "Optional HTML shell for Emscripten (defaults to cmake/web/shell.html)")
set(ENGINE_HOST_ASSET_CODEGEN "" CACHE FILEPATH "Native asset_codegen when cross-compiling (Emscripten or Android)")
set(ENGINE_HOST_ICON_CODEGEN "" CACHE FILEPATH "Native icon_codegen when cross-compiling (Emscripten or Android)")
# Gradle passes -DENGINE_ANDROID_ASSETS_OUT=${project.buildDir}/wind-assets and packs that same tree.
set(ENGINE_ANDROID_ASSETS_OUT "" CACHE PATH
    "Cooked APK assets directory shared with Gradle sourceSets (empty: stage beside libmain.so only)")

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
            "WINDOW deps: external/SDL3, external/glad, external/nanovg, external/boost_di.\n"
            "AUDIO deps: external/SDL3, external/SDL_mixer.\n"
            "See external/README.md")
    endif()
endfunction()

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
add_subdirectory(external/tinyxml2)
add_subdirectory(external/spdlog)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "[/\\\\]render[/\\\\]opengl[/\\\\]")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "engine_runtime\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "engine_instantiate\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "sdl_fatal_error\\.cpp$")

add_library(engine STATIC ${ENGINE_SOURCES})
add_library(engine::engine ALIAS engine)

target_include_directories(engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

# glm is part of the public API (IGame, Transform). spdlog/tinyxml2/toml stay PRIVATE (SDD §3.4).
target_link_libraries(engine
    PUBLIC
        glm::glm
    PRIVATE
        spdlog::spdlog
        tinyxml2::tinyxml2
        tomlplusplus::tomlplusplus)

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

# Copy assets/engine (builtins + cooked catalog) and, when present, the consumer's assets/
# beside a runtime target. Games normally call `engine_add_game` instead of this directly.
function(engine_prepare_runtime target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "engine_prepare_runtime: target '${target}' does not exist")
    endif()
    add_dependencies(${target} engine_builtin_catalog)

    set(_engine_game_assets "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    if(EXISTS "${_engine_game_assets}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_engine_game_assets}"
                "$<TARGET_FILE_DIR:${target}>/assets"
            COMMENT "Copy game assets beside ${target}"
            VERBATIM)
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ENGINE_BUILTIN_ASSETS_DIR}"
            "$<TARGET_FILE_DIR:${target}>/assets/engine"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ENGINE_COOKED_CATALOG}"
            "$<TARGET_FILE_DIR:${target}>/assets/engine/catalog.toml"
        COMMENT "Copy engine assets and cooked catalog beside ${target}"
        VERBATIM)

    get_property(_game_catalog TARGET ${target} PROPERTY ENGINE_GAME_CATALOG)
    if(_game_catalog)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_game_catalog}"
                "$<TARGET_FILE_DIR:${target}>/assets/catalog.toml"
            COMMENT "Install cooked game catalog for ${target}"
            VERBATIM)
    endif()

    if(ANDROID AND ENGINE_ANDROID_ASSETS_OUT)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${ENGINE_ANDROID_ASSETS_OUT}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_FILE_DIR:${target}>/assets"
                "${ENGINE_ANDROID_ASSETS_OUT}"
            COMMENT "Stage cooked APK assets to ENGINE_ANDROID_ASSETS_OUT"
            VERBATIM)
    endif()
endfunction()

# Windowed game: executable + C++23 + asset_codegen + runtime asset copy.
# Call after add_subdirectory(external/engine). Remaining args are sources.
function(engine_add_game target)
    if(NOT ARGN)
        message(FATAL_ERROR "engine_add_game(${target}): pass at least one source file")
    endif()
    if(NOT TARGET engine)
        message(FATAL_ERROR "engine_add_game: add_subdirectory the Wind engine first")
    endif()
    if(NOT ENGINE_WITH_WINDOW)
        message(FATAL_ERROR
            "engine_add_game(${target}): ENGINE_WITH_WINDOW is OFF. "
            "It defaults ON when Wind is a subdirectory.")
    endif()

    if(ANDROID)
        add_library(${target} SHARED ${ARGN})
        set_target_properties(${target} PROPERTIES OUTPUT_NAME main)
    else()
        add_executable(${target} ${ARGN})
    endif()
    if(APPLE)
        # Package as a .app bundle so MACOSX_BUNDLE_ICON_FILE / MACOSX_PACKAGE_LOCATION below
        # (SDD §19.2) take effect; ignored by CMake on non-Apple platforms either way.
        set_target_properties(${target} PROPERTIES MACOSX_BUNDLE ON)
    endif()
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
    if(ANDROID)
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endif()
    target_link_libraries(${target} PRIVATE engine)

    set(_include "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(EXISTS "${_include}")
        target_include_directories(${target} PRIVATE "${_include}")
    endif()

    set(_assets "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    if(EXISTS "${_assets}")
        set(_gen "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
        file(GLOB_RECURSE _asset_files CONFIGURE_DEPENDS "${_assets}/*")
        add_custom_command(
            OUTPUT "${_gen}/asset_ids.h" "${_gen}/catalog.toml"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen}"
            COMMAND asset_codegen "${_assets}" "${_gen}"
            DEPENDS asset_codegen ${_asset_files}
            COMMENT "Cook asset catalog for ${target}"
            VERBATIM)
        add_custom_target(${target}_assets DEPENDS
            "${_gen}/asset_ids.h"
            "${_gen}/catalog.toml")
        add_dependencies(${target} ${target}_assets)
        target_include_directories(${target} PRIVATE "${_gen}")
        set_property(TARGET ${target} PROPERTY ENGINE_GAME_CATALOG "${_gen}/catalog.toml")
        set_property(TARGET ${target} PROPERTY ENGINE_GAME_ASSETS "${_assets}")
    endif()

    set(_icon "${CMAKE_CURRENT_SOURCE_DIR}/icon.png")
    if(EXISTS "${_icon}")
        if(NOT TARGET icon_codegen)
            message(FATAL_ERROR "engine_add_game(${target}): icon.png present but icon_codegen is not a target")
        endif()
        set(_icon_gen "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}/icons")
        set(_icon_outputs
            "${_icon_gen}/icon.ico"
            "${_icon_gen}/icon.icns"
            "${_icon_gen}/favicon.png"
            "${_icon_gen}/mipmap-mdpi/ic_launcher.png"
            "${_icon_gen}/mipmap-hdpi/ic_launcher.png"
            "${_icon_gen}/mipmap-xhdpi/ic_launcher.png"
            "${_icon_gen}/mipmap-xxhdpi/ic_launcher.png"
            "${_icon_gen}/mipmap-xxxhdpi/ic_launcher.png")
        add_custom_command(
            OUTPUT ${_icon_outputs}
            COMMAND icon_codegen "${_icon}" "${_icon_gen}"
            DEPENDS icon_codegen "${_icon}"
            COMMENT "Generate platform app icons for ${target}"
            VERBATIM)
        add_custom_target(${target}_icons DEPENDS ${_icon_outputs})
        add_dependencies(${target} ${target}_icons)
        set_property(TARGET ${target} PROPERTY ENGINE_GAME_ICON_DIR "${_icon_gen}")
    endif()

    get_property(_icon_dir TARGET ${target} PROPERTY ENGINE_GAME_ICON_DIR)
    if(WIN32 AND _icon_dir)
        # icon.ico does not exist at configure time (icon_codegen writes it at build time), so
        # gate on the target property set above rather than EXISTS; forward slashes in the .rc
        # path avoid fighting rc.exe's backslash-escaping rules.
        set(_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}/icon.rc")
        file(GENERATE OUTPUT "${_icon_rc}" CONTENT "IDI_ICON1 ICON \"${_icon_dir}/icon.ico\"\n")
        target_sources(${target} PRIVATE "${_icon_rc}")
    endif()
    if(APPLE AND _icon_dir)
        # Consumes the shared icon_codegen output above (ENGINE_GAME_ICON_DIR) rather than
        # re-invoking icon_codegen — a second add_custom_command writing icon.icns would race it.
        set(_icns "${_icon_dir}/icon.icns")
        set_source_files_properties("${_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(${target} PRIVATE "${_icns}")
        set_target_properties(${target} PROPERTIES MACOSX_BUNDLE_ICON_FILE "icon.icns")
    endif()

    engine_prepare_runtime(${target})
    if(EMSCRIPTEN)
        include("${ENGINE_CMAKE_DIR}/cmake/web/link_flags.cmake")
        engine_target_web_link_options(${target})
        engine_target_web_preload(${target})
        # Re-fetch rather than reuse the _icon_gen local above: it's only set when icon.png
        # exists, and this block must stay a no-op otherwise.
        get_property(_icon_dir TARGET ${target} PROPERTY ENGINE_GAME_ICON_DIR)
        if(_icon_dir)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_icon_dir}/favicon.png"
                    "$<TARGET_FILE_DIR:${target}>/favicon.png"
                COMMENT "Copy web favicon beside ${target}"
                VERBATIM)
        endif()
    endif()
    if(ANDROID AND TARGET SDL3::SDL3main)
        target_link_libraries(${target} PRIVATE SDL3::SDL3main)
    endif()
    if(ANDROID)
        # SDLActivity's nativeRunMain() dlsym()s "SDL_main" specifically; a game's plain
        # int main() never gets renamed (no <SDL3/SDL_main.h> in game code, by design —
        # see engine boundary rules), so alias it at link time instead.
        target_link_options(${target} PRIVATE "SHELL:-Wl,--defsym=SDL_main=main")
    endif()
endfunction()

function(engine_add_web_game target)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "engine_add_web_game(${target}): configure with emcmake or the Emscripten toolchain")
    endif()
    engine_add_game(${target} ${ARGN})
endfunction()

function(engine_add_android_game target)
    if(NOT ANDROID)
        message(FATAL_ERROR
            "engine_add_android_game(${target}): configure with the Android NDK toolchain")
    endif()
    engine_add_game(${target} ${ARGN})
endfunction()

function(engine_add_sdl3)
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
    engine_require_file(external/boost_di/include/boost/di.hpp)

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
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine_instantiate.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/sdl_fatal_error.cpp")
    target_compile_definitions(engine PUBLIC ENGINE_WITH_WINDOW=1)
    target_include_directories(engine
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/external/boost_di/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/boost_di/extension/include"
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

if(ENGINE_BUILD_TESTS)
    engine_require_submodule(googletest)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    enable_testing()
    add_subdirectory(external/googletest)
    include(GoogleTest)

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
