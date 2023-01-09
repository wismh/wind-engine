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

if(ENGINE_WITH_WINDOW)
    engine_require_submodule(SDL3)
    message(FATAL_ERROR "ENGINE_WITH_WINDOW is slice 8: add SDL3/glad/nanovg per external/README.md, then remove this FATAL_ERROR.")
endif()

if(ENGINE_WITH_AUDIO)
    engine_require_submodule(SDL_mixer)
    message(FATAL_ERROR "ENGINE_WITH_AUDIO is slice 9: add SDL_mixer per external/README.md, then remove this FATAL_ERROR.")
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
    gtest_discover_tests(engine_tests DISCOVERY_MODE PRE_TEST)
endif()
