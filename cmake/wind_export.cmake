# Export builds of an editor SDK (-DWIND_EXPORT=ON), for WindConfig.cmake. The SDK installs this file beside
# WindConfig.cmake as <sdk>/cmake/wind_export.cmake. docs/tech/build/CMake.md
#
# An export links the game into a standalone executable with the static engine. Two sources for that engine:
#   the player libraries   <sdk>/player/Development and <sdk>/player/Shipping: the engine and its static libraries,
#                          built when the SDK was (cmake/player_build.cmake). The game compiles and links: seconds.
#   <sdk>/source           the engine's build input, compiled for the game on every export: minutes. The fallback when
#                          the SDK has no player for this platform (a Debug SDK, macOS), and what Web and Android
#                          builds always use.
# The player libraries serve two game configurations: DebugGame links Development, Release links Shipping.

set(WIND_EXPORT_FLAVOURS Development Shipping)

# Explains why the SDK at `sdk_dir` (configuration `sdk_config`) has no usable player libraries, or sets `out` empty
# when it has both flavours.
function(wind_export_player_problem sdk_dir sdk_config out)
    set(_problem "")
    if(sdk_config STREQUAL "Debug")
        set(_problem "a Debug SDK ships no player libraries")
    elseif(DEFINED ENGINE_WITH_AUDIO AND NOT ENGINE_WITH_AUDIO)
        # The players have the mixer and there is no variant without it: a game that does not want it builds the engine.
        set(_problem "ENGINE_WITH_AUDIO is OFF and the player libraries have the mixer")
    else()
        foreach(_flavour IN LISTS WIND_EXPORT_FLAVOURS)
            set(_dir "${sdk_dir}/player/${_flavour}")
            if(NOT EXISTS "${_dir}/cmake/wind_player.cmake" OR NOT EXISTS "${_dir}/cmake/wind_player_targets.cmake")
                set(_problem "${sdk_dir}/player/${_flavour} is missing")
                break()
            endif()
        endforeach()
    endif()
    set(${out} "${_problem}" PARENT_SCOPE)
endfunction()

# kBuildId of the SDK, read from its <engine/build_id.h>.
function(wind_export_sdk_build_id sdk_dir out)
    file(STRINGS "${sdk_dir}/include/engine/build_id.h" _line REGEX "kBuildIdCStr\\[\\] = \"[0-9a-f]+\"")
    if(NOT _line MATCHES "\"([0-9a-f]+)\"")
        message(FATAL_ERROR "Wind SDK at ${sdk_dir} is incomplete: no kBuildIdCStr in include/engine/build_id.h")
    endif()
    set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

# Imports the player libraries as `engine` (an INTERFACE library): the target of the Development flavour for the
# DebugGame configuration, the Shipping one for every other. Each flavour is a set of imported targets in the namespace
# WindPlayer<Flavour>:: with the compile definitions the flavour was built with; a flavour of another version, or
# installed by another SDK build, is a configure error. `engine` takes glm::glm and the include directory from the SDK.
function(wind_export_import_player sdk_dir)
    wind_export_sdk_build_id("${sdk_dir}" _sdk_build_id)
    # Targets the exported files find (OpenGL::GL, Threads::Threads) are as visible as `engine` is. The players have one
    # configuration, Release; the game's DebugGame links it too.
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    set(CMAKE_MAP_IMPORTED_CONFIG_DEBUGGAME Release)
    foreach(_flavour IN LISTS WIND_EXPORT_FLAVOURS)
        set(_dir "${sdk_dir}/player/${_flavour}/cmake")
        include("${_dir}/wind_player.cmake")
        if(NOT WIND_PLAYER_FLAVOUR STREQUAL _flavour)
            message(FATAL_ERROR "Wind player ${sdk_dir}/player/${_flavour} is the ${WIND_PLAYER_FLAVOUR} flavour")
        endif()
        if(DEFINED Wind_VERSION AND NOT WIND_PLAYER_VERSION STREQUAL Wind_VERSION)
            message(FATAL_ERROR
                "Wind player ${sdk_dir}/player/${_flavour} is engine ${WIND_PLAYER_VERSION}, this SDK is ${Wind_VERSION}")
        endif()
        if(NOT WIND_PLAYER_SDK_BUILD_ID STREQUAL _sdk_build_id)
            message(FATAL_ERROR
                "Wind player ${sdk_dir}/player/${_flavour} belongs to another build of the SDK (build id "
                "${WIND_PLAYER_SDK_BUILD_ID}, this SDK is ${_sdk_build_id}). Install the SDK again.")
        endif()
        include("${_dir}/wind_player_targets.cmake")
        # The exported targets are directory-scoped; `engine` is global and links them, so they have to be too.
        file(READ "${_dir}/wind_player_targets.cmake" _targets)
        string(REGEX MATCHALL "add_library\\(WindPlayer[A-Za-z]+::[^ )]+ [A-Z]+ IMPORTED\\)" _declarations "${_targets}")
        foreach(_declaration IN LISTS _declarations)
            string(REGEX REPLACE "add_library\\(([^ )]+) .*" "\\1" _name "${_declaration}")
            set_property(TARGET ${_name} PROPERTY IMPORTED_GLOBAL TRUE)
        endforeach()
        if(NOT TARGET WindPlayer${_flavour}::engine)
            message(FATAL_ERROR "Wind player ${sdk_dir}/player/${_flavour} defines no WindPlayer${_flavour}::engine")
        endif()
    endforeach()

    add_library(engine INTERFACE IMPORTED GLOBAL)
    set_target_properties(engine PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${sdk_dir}/include"
        INTERFACE_COMPILE_FEATURES cxx_std_23
        INTERFACE_LINK_LIBRARIES
            "glm::glm;$<$<CONFIG:DebugGame>:WindPlayerDevelopment::engine>;$<$<NOT:$<CONFIG:DebugGame>>:WindPlayerShipping::engine>")
endfunction()
