# Emscripten / WebAssembly toolchain for Wind.
#
# Preferred configure (sets the compiler without this file):
#   emcmake cmake --preset web
#
# Direct toolchain (requires EMSDK):
#   cmake --preset web --toolchain cmake/toolchains/Emscripten.cmake
# or
#   cmake -S . -B build-web -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/Emscripten.cmake

if(NOT DEFINED ENV{EMSDK} OR "$ENV{EMSDK}" STREQUAL "")
    message(FATAL_ERROR
        "Emscripten toolchain requires the EMSDK environment variable.\n"
        "Install emsdk, then either:\n"
        "  source emsdk_env.sh && emcmake cmake --preset web\n"
        "or set EMSDK to the emsdk root and pass this file as CMAKE_TOOLCHAIN_FILE.")
endif()

set(_engine_emscripten_cmake "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
if(NOT EXISTS "${_engine_emscripten_cmake}")
    message(FATAL_ERROR
        "Could not find ${_engine_emscripten_cmake}.\n"
        "Is EMSDK pointing at an emsdk installation?")
endif()

include("${_engine_emscripten_cmake}")
