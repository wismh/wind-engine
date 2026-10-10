# Builds the player libraries of the editor SDK: one configure, build, and install of the static engine per flavour
# (ENGINE_PLAYER in the root CMakeLists.txt), in a build tree of its own, so the editor build's shared engine and the
# players never share a cache. Run by CMakeLists.txt, in two places:
#   target wind_player                 configure and build (nothing installed; fills <build-editor>/player/<flavour>)
#   cmake --install (SDK, Release)     the same, then install into <prefix>/player/<flavour>
# Both run `cmake -P cmake/player_build.cmake` with:
#   WIND_PLAYER_SOURCE_DIR       the engine checkout
#   WIND_PLAYER_BINARY_DIR       parent of the build trees (<build>/player)
#   WIND_PLAYER_PREFIX           where the flavours are installed (<sdk>/player); empty: build only
#   WIND_PLAYER_FLAVOURS         comma-separated: Development,Shipping
#   WIND_PLAYER_VERSION_PATCH    the editor build's ENGINE_VERSION_PATCH (empty for a local build), so the players have
#                                the version of the SDK
#   WIND_PLAYER_SDK_BUILD_ID_HEADER  the SDK's generated <engine/build_id.h>; its kBuildId is stamped into each player
#   WIND_PLAYER_GENERATOR, WIND_PLAYER_GENERATOR_PLATFORM, WIND_PLAYER_GENERATOR_TOOLSET, WIND_PLAYER_MAKE_PROGRAM,
#   WIND_PLAYER_C_COMPILER, WIND_PLAYER_CXX_COMPILER   the editor build's own, so the players use the same toolchain
# A second run finds every tree up to date and only installs. A failure ends the script with FATAL_ERROR, so a
# failed install says so instead of producing an SDK without a player. docs/tech/build/CMake.md

foreach(_var WIND_PLAYER_SOURCE_DIR WIND_PLAYER_BINARY_DIR WIND_PLAYER_FLAVOURS WIND_PLAYER_GENERATOR
        WIND_PLAYER_SDK_BUILD_ID_HEADER)
    if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
        message(FATAL_ERROR "player_build.cmake: -D${_var}=... is required")
    endif()
endforeach()

file(STRINGS "${WIND_PLAYER_SDK_BUILD_ID_HEADER}" _line REGEX "kBuildIdCStr\\[\\] = \"[0-9a-f]+\"")
if(NOT _line MATCHES "\"([0-9a-f]+)\"")
    message(FATAL_ERROR "player_build.cmake: no kBuildIdCStr in ${WIND_PLAYER_SDK_BUILD_ID_HEADER}")
endif()
set(_sdk_build_id "${CMAKE_MATCH_1}")

function(_wind_player_run what)
    message(STATUS "Wind player: ${what}")
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Wind player: ${what} failed (exit code ${_result})")
    endif()
endfunction()

set(_configure -G "${WIND_PLAYER_GENERATOR}")
if(WIND_PLAYER_GENERATOR_PLATFORM)
    list(APPEND _configure -A "${WIND_PLAYER_GENERATOR_PLATFORM}")
endif()
if(WIND_PLAYER_GENERATOR_TOOLSET)
    list(APPEND _configure -T "${WIND_PLAYER_GENERATOR_TOOLSET}")
endif()
if(NOT WIND_PLAYER_GENERATOR MATCHES "^Visual Studio")
    # Visual Studio picks its compiler itself; the others are told which one the editor build uses.
    foreach(_language IN ITEMS C CXX)
        if(WIND_PLAYER_${_language}_COMPILER)
            list(APPEND _configure "-DCMAKE_${_language}_COMPILER=${WIND_PLAYER_${_language}_COMPILER}")
        endif()
    endforeach()
    if(WIND_PLAYER_MAKE_PROGRAM)
        list(APPEND _configure "-DCMAKE_MAKE_PROGRAM=${WIND_PLAYER_MAKE_PROGRAM}")
    endif()
endif()
# One configuration per tree, so the exported targets of the player have one set of locations.
if(WIND_PLAYER_GENERATOR MATCHES "^Visual Studio|Multi-Config|Xcode")
    list(APPEND _configure -DCMAKE_CONFIGURATION_TYPES=Release)
else()
    list(APPEND _configure -DCMAKE_BUILD_TYPE=Release)
endif()

set(_build_tail)
if(WIND_PLAYER_GENERATOR MATCHES "^Visual Studio")
    set(_build_tail -- /nologo /verbosity:minimal)
endif()

string(REPLACE "," ";" WIND_PLAYER_FLAVOURS "${WIND_PLAYER_FLAVOURS}")
foreach(_flavour IN LISTS WIND_PLAYER_FLAVOURS)
    set(_tree "${WIND_PLAYER_BINARY_DIR}/${_flavour}")
    _wind_player_run("configure ${_flavour}"
        "${CMAKE_COMMAND}" -S "${WIND_PLAYER_SOURCE_DIR}" -B "${_tree}" ${_configure}
        -DENGINE_PLAYER=${_flavour}
        "-DENGINE_VERSION_PATCH=${WIND_PLAYER_VERSION_PATCH}"
        -DENGINE_BUILD_TESTS=OFF -DENGINE_WITH_GTEST=OFF
        -DENGINE_WITH_WINDOW=ON -DENGINE_WITH_AUDIO=ON
        -DWIND_PLAYER_SDK_BUILD_ID=${_sdk_build_id})
    _wind_player_run("build ${_flavour}"
        "${CMAKE_COMMAND}" --build "${_tree}" --config Release --target engine --parallel ${_build_tail})
    if(WIND_PLAYER_PREFIX)
        # A clean directory, so a library a newer engine no longer links does not stay behind.
        file(REMOVE_RECURSE "$ENV{DESTDIR}${WIND_PLAYER_PREFIX}/${_flavour}")
        _wind_player_run("install ${_flavour} into ${WIND_PLAYER_PREFIX}/${_flavour}"
            "${CMAKE_COMMAND}" --install "${_tree}" --config Release --prefix "${WIND_PLAYER_PREFIX}/${_flavour}")
    endif()
endforeach()
