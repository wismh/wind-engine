cmake_minimum_required(VERSION 3.20)
project({{target}} LANGUAGES CXX)

# The Wind SDK. The editor passes CMAKE_PREFIX_PATH when it builds the game; CMakeUserPresets.json does for an IDE.
find_package(Wind REQUIRED)

engine_add_game({{target}}
    src/main.cpp
    src/game.h
    src/game.cpp)
