cmake_minimum_required(VERSION 3.15)
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
option(ENGINE_WITH_WINDOW "SDL3 + OpenGL + NanoVG backend" OFF)
option(ENGINE_WITH_AUDIO "SDL3_mixer audio backend" OFF)

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

add_executable(asset_codegen "${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_codegen/main.cpp")
target_link_libraries(asset_codegen PRIVATE engine)

add_executable(asset_guid "${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_guid/main.cpp")
target_link_libraries(asset_guid PRIVATE engine)

if(ENGINE_WITH_WINDOW)
    engine_require_file(external/SDL3/CMakeLists.txt)
    engine_require_file(external/nanovg/src/nanovg.c)
    engine_require_file(external/boost_di/include/boost/di.hpp)

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/SDL3)

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

    add_library(nanovg STATIC "${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src/nanovg.c")
    target_include_directories(nanovg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src")
    if(MSVC)
        target_compile_definitions(nanovg PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    file(GLOB_RECURSE ENGINE_GL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/opengl/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/opengl/*.c")
    target_sources(engine PRIVATE ${ENGINE_GL_SOURCES})
    target_compile_definitions(engine PRIVATE ENGINE_WITH_WINDOW=1)
    target_include_directories(engine PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/opengl"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/boost_di/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/boost_di/extension/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/nanovg/src")
    find_package(OpenGL REQUIRED)
    target_link_libraries(engine PRIVATE SDL3::SDL3 glad nanovg OpenGL::GL)
endif()

if(ENGINE_WITH_AUDIO)
    engine_require_submodule(SDL_mixer)
    # Real MIX_* sources are not compiled yet. IAudioSystem policy + FakeMixer live in src/audio
    # and always link into engine (engine_tests has no audio device).
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
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/builtin_assets" _engine_builtin_assets)
    target_compile_definitions(engine_tests PRIVATE "ENGINE_BUILTIN_ASSETS_DIR=\"${_engine_builtin_assets}\"")
    add_custom_command(TARGET engine_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/builtin_assets"
            "$<TARGET_FILE_DIR:engine_tests>/assets/engine"
        COMMENT "Copy builtin_assets to engine_tests output dir")
    gtest_discover_tests(engine_tests DISCOVERY_MODE PRE_TEST)
endif()
