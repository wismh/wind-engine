# Game functions: engine_prepare_runtime, engine_configure_app, engine_add_game, engine_add_server, engine_add_web_game,
# engine_add_android_game. Included by the engine's root CMakeLists.txt (source build) and by an installed
# editor SDK's WindConfig.cmake (SDK mode, find_package(Wind)). docs/tech/build/CMake.md
#
# The functions run in the calling directory's scope, so everything they read from the engine is a cache
# variable that both contexts set:
#   ENGINE_FROM_SDK            ON when `engine`, `asset_codegen`, and `icon_codegen` are imported from an SDK
#   ENGINE_PLAYER_SDK          ON (with ENGINE_FROM_SDK) in an export build against the SDK's prebuilt static engine
#                              (WIND_EXPORT, <sdk>/player/<flavour>/): the game is an executable, not a module
#   ENGINE_BUILTIN_ASSETS_DIR  builtin assets copied to <output>/assets/engine/
#   ENGINE_COOKED_CATALOG      cooked catalog.toml for those builtins
#   ENGINE_CMAKE_DIR           the engine source tree (cmake/web/link_flags.cmake)
#   ENGINE_GNS_RUNTIME_DLLS    on Windows, the GameNetworkingSockets DLLs copied beside a server or a game's executable
#   ENGINE_SLANG_RUNTIME_DLLS  on Windows, the Slang DLLs copied beside a game's executable
# The source build also defines the target `engine_builtin_catalog`, which cooks ENGINE_COOKED_CATALOG. In
# the SDK the catalog is already cooked under bin/assets/engine/.

# SDK mode only, called by WindConfig.cmake: check the game's configurations against the SDK's and give DebugGame its
# flags. A module shares the C runtime with the SDK's engine.dll, so:
#   Release (any non-Debug) SDK: the configurations DebugGame and Release, both /MD. Debug, RelWithDebInfo, and
#     MinSizeRel are refused. CMake has no flags for DebugGame, so the CMAKE_<LANG>_FLAGS_DEBUGGAME and
#     CMAKE_<KIND>_LINKER_FLAGS_DEBUGGAME cache entries below (filled when empty) give every target of the game's
#     tree (the module, the game's own tests, googletest) /Od /Ob0 /Zi /RTC1 with /MD and no NDEBUG or _DEBUG.
#     They are cache entries, so they reach the game's directories, and a game can still set them in its preset.
#   Debug SDK: the configuration Debug only, with CMake's Debug flags (/MDd).
# The list has to be set before the game's project(), so it comes from the game's editor preset
# (CMAKE_CONFIGURATION_TYPES, or CMAKE_BUILD_TYPE for a single-configuration generator). This only checks it.
# An export build against the SDK's player libraries (ENGINE_PLAYER_SDK) calls it too: the same configurations and CRT,
# DebugGame linked with the Development player and Release with the Shipping one.
function(engine_sdk_configurations)
    if(WIND_SDK_CONFIG STREQUAL "Debug")
        set(_allowed Debug)
        set(_hint "A Debug SDK has the debug CRT, so the game builds in Debug only.")
    else()
        set(_allowed DebugGame Release)
        set(_hint
            "A ${WIND_SDK_CONFIG} SDK has the release CRT, so the game builds in DebugGame (game code unoptimized, "
            "with symbols, asserts on) or Release, both with /MD. Debug would load the debug CRT into a release "
            "engine.dll. "
            "To debug the engine itself, install a Debug SDK (cmake --install build-editor --config Debug).")
    endif()
    get_property(_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_multi)
        set(_variable CMAKE_CONFIGURATION_TYPES)
    else()
        set(_variable CMAKE_BUILD_TYPE)
    endif()
    set(_configs ${${_variable}})
    set(_refused)
    foreach(_config IN LISTS _configs)
        if(NOT _config IN_LIST _allowed)
            list(APPEND _refused "${_config}")
        endif()
    endforeach()
    if(_refused OR NOT _configs)
        string(JOIN ";" _wanted ${_allowed})
        string(JOIN "" _hint ${_hint})
        message(FATAL_ERROR
            "Wind SDK mode: ${_variable} is \"${_configs}\". Against this ${WIND_SDK_CONFIG} SDK it must be "
            "\"${_wanted}\" (or a part of it). ${_hint}\n"
            "Set it in the game's editor preset, before the first configure:\n"
            "  \"cacheVariables\": { \"${_variable}\": \"${_wanted}\" }\n"
            "or pass -D${_variable}=${_wanted} to a fresh configure. See docs/tech/build/Game Consumer.md")
    endif()

    if(WIND_SDK_CONFIG STREQUAL "Debug")
        return()
    endif()
    engine_debuggame_flags()
endfunction()

# The DebugGame flags as cache defaults, and the release DLL CRT. SDK mode and the export builds (the SDK's player
# libraries, or the engine built from <sdk>/source when the SDK has none) call it: CMake has no flags for a
# configuration it does not know.
function(engine_debuggame_flags)
    # Every game target uses the release DLL CRT. Targets whose directory sets CMP0091 OLD (googletest asks for
    # CMake 3.13) read the runtime from the flags instead, hence /MD in the DebugGame flags too.
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL CACHE STRING "Wind SDK mode: the release DLL CRT of engine.dll")
    if(MSVC)
        set(_compile "/MD /Od /Ob0 /Zi /RTC1")
        set(_link "/DEBUG /INCREMENTAL")
    else()
        set(_compile "-O0 -g")
        set(_link "")
    endif()
    # project() already created these as empty cache entries (it does that for every configuration it does not
    # know), so an empty one is filled with FORCE. A value the game set in its preset stays.
    set(_names)
    set(_values)
    foreach(_lang IN ITEMS C CXX)
        list(APPEND _names CMAKE_${_lang}_FLAGS_DEBUGGAME)
        list(APPEND _values "${_compile}")
    endforeach()
    foreach(_kind IN ITEMS EXE SHARED MODULE)
        list(APPEND _names CMAKE_${_kind}_LINKER_FLAGS_DEBUGGAME)
        list(APPEND _values "${_link}")
    endforeach()
    foreach(_name _value IN ZIP_LISTS _names _values)
        if("${${_name}}" STREQUAL "")
            set(${_name} "${_value}" CACHE STRING "Wind SDK mode: DebugGame flags" FORCE)
        endif()
    endforeach()
endfunction()

# Under the old behaviour the stage command below, which names the target's directory, would depend on the target:
# a cycle, in a game whose cmake_minimum_required is older than 3.19.
cmake_policy(SET CMP0112 NEW)

# Copy assets/engine (builtins + cooked catalog) and, when present, the consumer's assets/
# beside a runtime target. Games normally call `engine_add_game` instead of this directly.
# A game module built against the SDK gets only its own assets/: the editor that loads it has engine.dll and
# assets/engine/ beside itself.
function(engine_prepare_runtime target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "engine_prepare_runtime: target '${target}' does not exist")
    endif()
    get_target_property(_target_type ${target} TYPE)
    set(_engine_side ON)
    if(ENGINE_FROM_SDK AND _target_type STREQUAL "SHARED_LIBRARY")
        set(_engine_side OFF)
    endif()
    if(NOT ENGINE_FROM_SDK)
        add_dependencies(${target} engine_builtin_catalog)
    endif()

    # Editor build on Windows: the loader looks for engine.dll beside the executable. A game's
    # bin/ is not engine's output directory when Wind is a subdirectory, so copy it there.
    get_target_property(_engine_type engine TYPE)
    get_property(_server_app TARGET ${target} PROPERTY ENGINE_SERVER_APP)
    # A server against the SDK links the static Server player, whatever `engine` is there.
    set(_copy_engine_dll OFF)
    if(WIN32 AND _engine_side AND _engine_type STREQUAL "SHARED_LIBRARY" AND NOT (ENGINE_FROM_SDK AND _server_app))
        set(_copy_engine_dll ON)
    endif()

    # ENGINE_RUNTIME_ASSETS_DIR puts the target's own assets and catalog in assets/<dir>/ instead of
    # assets/. The editor uses assets/editor/, so a game module in the same bin/ keeps assets/ to itself.
    get_property(_runtime_assets_dir TARGET ${target} PROPERTY ENGINE_RUNTIME_ASSETS_DIR)
    if(_runtime_assets_dir)
        set(_engine_app_assets_out "$<TARGET_FILE_DIR:${target}>/assets/${_runtime_assets_dir}")
    else()
        set(_engine_app_assets_out "$<TARGET_FILE_DIR:${target}>/assets")
    endif()

    if(_target_type STREQUAL "SHARED_LIBRARY" AND NOT WIN32)
        get_target_property(_runtime_dir ${target} LIBRARY_OUTPUT_DIRECTORY)
    else()
        get_target_property(_runtime_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
    endif()
    if(NOT _runtime_dir)
        set(_runtime_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    get_target_property(_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND _bundle)
        set(_runtime_dir "${_runtime_dir}/${target}.app")
    endif()

    # Staging is a target of its own, ordered before ${target}: it follows the asset files, not the link, because an
    # edited asset leaves asset_ids.h as it was and so relinks nothing. It writes only the staged tree. The sole writer
    # of generated/<target>/ is the custom command of <target>_assets in engine_configure_app, so nothing rewrites a
    # header while another target compiles against it. Targets that stage the same assets to the same place (a game and
    # its server, the editor and its tests) share the first one's stage, so that place also has one writer.
    set(_engine_game_assets "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    if(EXISTS "${_engine_game_assets}")
        string(MD5 _stage_key "${_runtime_dir};${_runtime_assets_dir};${_engine_game_assets}")
        get_property(_stage GLOBAL PROPERTY ENGINE_STAGE_${_stage_key})
        if(NOT _stage)
            set(_stage ${target}_stage)
            set_property(GLOBAL PROPERTY ENGINE_STAGE_${_stage_key} ${_stage})
            file(GLOB_RECURSE _engine_game_asset_files CONFIGURE_DEPENDS "${_engine_game_assets}/*")
            set(_stage_stamp "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}/stage.$<CONFIG>.stamp")
            set(_stage_extra)
            if(ANDROID AND ENGINE_ANDROID_ASSETS_OUT)
                set(_stage_extra
                    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_engine_app_assets_out}" "${ENGINE_ANDROID_ASSETS_OUT}")
            endif()
            add_custom_command(
                OUTPUT "${_stage_stamp}"
                COMMAND asset_codegen
                    "${_engine_game_assets}"
                    --stage "${_engine_app_assets_out}"
                    --project "${CMAKE_CURRENT_SOURCE_DIR}"
                ${_stage_extra}
                COMMAND ${CMAKE_COMMAND} -E touch "${_stage_stamp}"
                DEPENDS asset_codegen ${_engine_game_asset_files}
                COMMENT "Stage cooked game assets beside ${target}"
                VERBATIM)
            add_custom_target(${_stage} DEPENDS "${_stage_stamp}")
        endif()
        add_dependencies(${target} ${_stage})
    endif()

    # What the engine brings is the same for every target in one output directory (bin/ holds engine_tests, wind_editor,
    # wind_editor_tests, and the server fixture), so that directory gets one step that they all depend on. A POST_BUILD
    # on each would write the same files in parallel and fail with "Permission denied" on Windows.
    if(_engine_side)
        string(MD5 _runtime_key "${_runtime_dir}")
        set(_runtime_step engine_runtime_${_runtime_key})
        if(NOT TARGET ${_runtime_step})
            file(GLOB_RECURSE _engine_builtin_files CONFIGURE_DEPENDS "${ENGINE_BUILTIN_ASSETS_DIR}/*")
            set(_runtime_stamp "${CMAKE_BINARY_DIR}/generated/engine_runtime/${_runtime_key}.$<CONFIG>.stamp")
            set(_copy_dll_command)
            set(_copy_dll_depends)
            if(_copy_engine_dll)
                set(_copy_dll_command
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:engine>" "$<TARGET_FILE_DIR:${target}>")
                set(_copy_dll_depends engine)
            endif()
            add_custom_command(
                OUTPUT "${_runtime_stamp}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${ENGINE_BUILTIN_ASSETS_DIR}"
                    "$<TARGET_FILE_DIR:${target}>/assets/engine"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ENGINE_COOKED_CATALOG}"
                    "$<TARGET_FILE_DIR:${target}>/assets/engine/catalog.toml"
                ${_copy_dll_command}
                COMMAND ${CMAKE_COMMAND} -E touch "${_runtime_stamp}"
                DEPENDS "${ENGINE_COOKED_CATALOG}" ${_engine_builtin_files} ${_copy_dll_depends}
                COMMENT "Copy engine assets and cooked catalog beside ${target}"
                VERBATIM)
            add_custom_target(${_runtime_step} DEPENDS "${_runtime_stamp}")
            if(NOT ENGINE_FROM_SDK)
                add_dependencies(${_runtime_step} engine_builtin_catalog)
            endif()
        endif()
        add_dependencies(${target} ${_runtime_step})
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

# Everything engine_add_game does once the target exists, shared with the editor (editor/CMakeLists.txt):
# C++23, MSVC warnings, bin/ output, the engine link, include/, cooked assets/ (asset_ids.h and
# catalog.toml), icon.png, the runtime copy beside the output, and the web and Android link steps. Reads
# include/, assets/, and icon.png from the calling directory. An executable gets the console rule below;
# a game module (editor build or SDK mode) gets /PDBALTPATH.
function(engine_configure_app target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "engine_configure_app: target '${target}' does not exist")
    endif()
    get_target_property(_type ${target} TYPE)
    set(_module OFF)
    if((ENGINE_EDITOR OR ENGINE_FROM_SDK) AND _type STREQUAL "SHARED_LIBRARY")
        set(_module ON)
    endif()
    # engine_add_server: a console program with no window, so no icon, linking the server's engine.
    get_property(_server_app TARGET ${target} PROPERTY ENGINE_SERVER_APP)
    if(APPLE)
        # Package as a .app bundle so MACOSX_BUNDLE_ICON_FILE / MACOSX_PACKAGE_LOCATION below
        # take effect; ignored by CMake on non-Apple platforms either way. A target that sets MACOSX_BUNDLE OFF
        # first (the editor, a server) lands in bin/ itself, which engine_prepare_runtime has to know.
        get_property(_bundle_chosen TARGET ${target} PROPERTY MACOSX_BUNDLE SET)
        if(NOT _bundle_chosen)
            set_target_properties(${target} PROPERTIES MACOSX_BUNDLE ON)
        endif()
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
    if(_module)
        # A .dll is a RUNTIME output, a .so a LIBRARY output: both go where the executable would. The
        # import library nobody links against stays out of bin/.
        if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
            set_target_properties(${target} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        endif()
        if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
            set_target_properties(${target} PROPERTIES
                ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
        endif()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endif()
    if(WIN32 AND MSVC AND _module)
        # The editor loads a copy of the module with its .pdb beside it. Embed only the PDB file name
        # so the debugger finds that copy and the original stays unlocked for the next build. This is
        # what /PDBALTPATH:%_PDB% means. %_PDB% is an MSBuild macro, so the name comes from the generator
        # expression instead and the debugger finds the copied PDB.
        target_link_options(${target} PRIVATE "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${target}>")
    elseif(WIN32 AND MSVC AND _type STREQUAL "EXECUTABLE" AND NOT _server_app)
        # Release-ish configs hide the console window (players shouldn't see a terminal); Debug
        # keeps it so spdlog output is visible live. /ENTRY:mainCRTStartup keeps the ordinary
        # int main(int, char**) entry point instead of requiring a WinMain — the CRT startup
        # routine mainCRTStartup already parses argv and calls main() either way, /SUBSYSTEM just
        # controls whether the OS allocates a console for the process.
        target_link_options(${target} PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>"
            "$<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>")
    endif()
    if(_server_app)
        target_link_libraries(${target} PRIVATE engine_server)
    else()
        target_link_libraries(${target} PRIVATE engine)
    endif()

    set(_include "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(EXISTS "${_include}")
        target_include_directories(${target} PRIVATE "${_include}")
    endif()
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(EXISTS "${_src}")
        target_include_directories(${target} PRIVATE "${_src}")
    endif()

    set(_gen "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
    target_include_directories(${target} PRIVATE "${_gen}")

    file(GLOB_RECURSE _component_headers CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
    add_custom_command(
        OUTPUT "${_gen}/component_io.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen}"
        COMMAND asset_codegen --components "${_gen}/component_io.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
        DEPENDS asset_codegen ${_component_headers}
        COMMENT "Generate component serializers for ${target}"
        VERBATIM)
    add_custom_target(${target}_components DEPENDS "${_gen}/component_io.h")
    add_dependencies(${target} ${target}_components)

    set(_assets "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    if(EXISTS "${_assets}")
        file(GLOB_RECURSE _asset_files CONFIGURE_DEPENDS "${_assets}/*")
        add_custom_command(
            OUTPUT "${_gen}/asset_ids.h" "${_gen}/catalog.toml"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen}"
            COMMAND asset_codegen "${_assets}" "${_gen}" --project "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS asset_codegen ${_asset_files}
            COMMENT "Cook asset catalog for ${target}"
            VERBATIM)
        add_custom_target(${target}_assets DEPENDS
            "${_gen}/asset_ids.h"
            "${_gen}/catalog.toml")
        add_dependencies(${target} ${target}_assets)
        set_property(TARGET ${target} PROPERTY ENGINE_GAME_CATALOG "${_gen}/catalog.toml")
        set_property(TARGET ${target} PROPERTY ENGINE_GAME_ASSETS "${_assets}")
        if(EMSCRIPTEN)
            # The link packs the assets (engine_target_web_preload), so an edited asset has to relink.
            set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${_asset_files})
        endif()
    endif()

    set(_icon "${CMAKE_CURRENT_SOURCE_DIR}/icon.png")
    if(EXISTS "${_icon}" AND NOT _server_app)
        if(NOT TARGET icon_codegen)
            message(FATAL_ERROR "engine_configure_app(${target}): icon.png present but icon_codegen is not a target")
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

    set(_manifest "${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidManifest.xml")
    if(NOT EXISTS "${_manifest}")
        set(_manifest "${CMAKE_CURRENT_SOURCE_DIR}/AndroidManifest.xml")
    endif()
    if(EXISTS "${_manifest}")
        set_property(TARGET ${target} PROPERTY ENGINE_GAME_ANDROID_MANIFEST "${_manifest}")
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

# Windowed game: executable + C++23 + asset_codegen + runtime asset copy.
# Call after find_package(Wind) (or, for a static build, add_subdirectory of the engine source). Remaining args are
# sources. Under ENGINE_EDITOR, and always in SDK mode, the game is a shared module instead
# (ENGINE_GAME_MODULE, see <engine/game_entry.h>) that the editor loads on Play; except in an export build against the
# SDK's player libraries (ENGINE_PLAYER_SDK), which is an executable again.
function(engine_add_game target)
    if(NOT ARGN)
        message(FATAL_ERROR "engine_add_game(${target}): pass at least one source file")
    endif()
    if(NOT TARGET engine)
        message(FATAL_ERROR "engine_add_game: find_package(Wind) first")
    endif()
    if(NOT ENGINE_FROM_SDK AND NOT ENGINE_WITH_WINDOW)
        message(FATAL_ERROR
            "engine_add_game(${target}): ENGINE_WITH_WINDOW is OFF. "
            "It defaults ON when Wind is a subdirectory.")
    endif()

    if(ANDROID)
        add_library(${target} SHARED ${ARGN})
        set_target_properties(${target} PROPERTIES OUTPUT_NAME main)
    elseif((ENGINE_EDITOR OR ENGINE_FROM_SDK) AND NOT ENGINE_PLAYER_SDK)
        # ENGINE_GAME expands to the wind_create_game / wind_destroy_game / wind_game_build_id exports.
        add_library(${target} SHARED ${ARGN})
        target_compile_definitions(${target} PRIVATE ENGINE_GAME_MODULE=1)
        set_target_properties(${target} PROPERTIES PREFIX "")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # libstdc++ marks inline variables and template statics STB_GNU_UNIQUE, which keeps a module mapped after
            # dlclose and binds the next Play's copy to the old module's statics.
            target_compile_options(${target} PRIVATE -fno-gnu-unique)
        endif()
        if(ENGINE_FROM_SDK AND NOT WIN32)
            # The module links the SDK's libengine by its SONAME / @rpath name. The editor process has it loaded
            # already; the rpath makes the module loadable on its own too (a debugger, the fixture tests).
            set_target_properties(${target} PROPERTIES BUILD_RPATH "${WIND_SDK_DIR}/bin")
        endif()
        # Where the module of each configuration lands, for the editor that builds and loads it: one line, the
        # absolute path, in <build>/wind/<target>.<config>.module. Written when CMake generates, so before a build.
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/wind/${target}.$<CONFIG>.module" CONTENT "$<TARGET_FILE:${target}>")
        if(ENGINE_FROM_SDK AND MSVC)
            # The CRT of the SDK's engine.dll, whatever the game's directory sets (engine_sdk_configurations has
            # the DebugGame flags). The Release module gets symbols, like the SDK: /Zi, and /DEBUG with /OPT:REF
            # and /OPT:ICF kept on. <engine/game_entry.h> checks the CRT at compile time.
            if(WIND_SDK_CONFIG STREQUAL "Debug")
                set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDebugDLL)
            else()
                set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
                target_compile_options(${target} PRIVATE "$<$<CONFIG:Release>:/Zi>")
                target_link_options(${target} PRIVATE
                    "$<$<CONFIG:Release>:/DEBUG>"
                    "$<$<CONFIG:Release>:/OPT:REF>"
                    "$<$<CONFIG:Release>:/OPT:ICF>")
            endif()
        endif()
    else()
        add_executable(${target} ${ARGN})
        if(ENGINE_PLAYER_SDK AND MSVC)
            # The CRT of the player libraries (and of the tools that cooked the assets).
            set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
        endif()
        if(NOT EMSCRIPTEN)
            # Where the executable of each configuration lands, for the editor's Export: one line, the absolute
            # directory, in <build>/wind/<target>.<config>.export. The directory also holds assets/.
            file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/wind/${target}.$<CONFIG>.export"
                CONTENT "$<TARGET_FILE_DIR:${target}>")
        endif()
    endif()
    engine_configure_app(${target})
    # A module runs beside the editor, which has them. An executable is on its own: Windows will not start it without
    # the DLLs it imports, and an Export copies this directory. GameNetworkingSockets goes too: the import appears only
    # when the game uses a transport, which CMake cannot tell here.
    get_target_property(_game_type ${target} TYPE)
    if(_game_type STREQUAL "EXECUTABLE")
        engine_copy_runtime_dlls(${target} ${ENGINE_SLANG_RUNTIME_DLLS})
        engine_copy_runtime_dlls(${target} ${ENGINE_GNS_RUNTIME_DLLS})
    endif()
endfunction()

# Copies one set of DLLs beside `target`, and is the only thing that does. Nothing off Windows, or with no DLLs. Several
# targets land in one directory and need the same set (the tools and tests in bin/, a game and its server), so each
# directory and set is one step that they all depend on: a POST_BUILD on each would write the same files in parallel, which
# fails on Windows.
function(engine_copy_runtime_dlls target)
    if(NOT WIN32 OR NOT ARGN)
        return()
    endif()
    get_target_property(_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _dir)
        set(_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    string(MD5 _key "${_dir};${ARGN}")
    set(_step engine_dlls_${_key})
    if(NOT TARGET ${_step})
        set(_stamp "${CMAKE_BINARY_DIR}/generated/engine_runtime/dlls_${_key}.$<CONFIG>.stamp")
        add_custom_command(
            OUTPUT "${_stamp}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ARGN} "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
            DEPENDS ${ARGN}
            COMMENT "Copy runtime DLLs beside ${target}"
            VERBATIM)
        add_custom_target(${_step} DEPENDS "${_stamp}")
    endif()
    add_dependencies(${target} ${_step})
endfunction()

# Dedicated server: always an executable, in every mode, that the game's source enters with ENGINE_GAME_SERVER
# (<engine/server_entry.h>). No window and no SDL: a console program in every configuration. In the source build it links
# `engine` (configure with ENGINE_WITH_WINDOW and ENGINE_WITH_AUDIO OFF for one without SDL). Against an SDK it links the
# Server player libraries (<sdk>/player/Server), beside the module of `engine_add_game` in the same tree; an SDK without
# them (a Debug SDK, macOS, Linux without the GameNetworkingSockets source) skips the target, so the game still configures
# for the editor. On Windows the GameNetworkingSockets DLLs land beside it.
function(engine_add_server target)
    if(NOT ARGN)
        message(FATAL_ERROR "engine_add_server(${target}): pass at least one source file")
    endif()
    if(NOT TARGET engine)
        message(FATAL_ERROR "engine_add_server: find_package(Wind) first")
    endif()
    if(EMSCRIPTEN OR ANDROID)
        message(FATAL_ERROR "engine_add_server(${target}): a dedicated server is a desktop program")
    endif()
    if(ENGINE_FROM_SDK AND NOT TARGET engine_server)
        wind_export_server_problem("${WIND_SDK_DIR}" "${WIND_SDK_CONFIG}" _problem)
        if(NOT _problem STREQUAL "")
            message(STATUS "engine_add_server(${target}): skipped, ${_problem}")
            return()
        endif()
        wind_export_import_server("${WIND_SDK_DIR}")
    endif()

    add_executable(${target} ${ARGN})
    set_property(TARGET ${target} PROPERTY ENGINE_SERVER_APP ON)
    if(ENGINE_FROM_SDK AND MSVC)
        # The CRT of the player libraries.
        set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    endif()
    # Where the executable of each configuration lands, the record an export has.
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/wind/${target}.$<CONFIG>.export" CONTENT "$<TARGET_FILE_DIR:${target}>")
    set_target_properties(${target} PROPERTIES MACOSX_BUNDLE OFF)
    engine_configure_app(${target})
    engine_copy_runtime_dlls(${target} ${ENGINE_GNS_RUNTIME_DLLS})
    # The Server player of an SDK has no Slang. The source build's engine does when it has a window.
    if(NOT ENGINE_FROM_SDK)
        engine_copy_runtime_dlls(${target} ${ENGINE_SLANG_RUNTIME_DLLS})
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
