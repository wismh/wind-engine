# Game functions: engine_prepare_runtime, engine_configure_app, engine_add_game, engine_add_web_game,
# engine_add_android_game. Included by the engine's root CMakeLists.txt (source build) and by an installed
# editor SDK's wind_sdk.cmake (SDK mode, WIND_EDITOR_SDK). docs/tech/build/CMake.md
#
# The functions run in the calling directory's scope, so everything they read from the engine is a cache
# variable that both contexts set:
#   ENGINE_FROM_SDK            ON when `engine`, `asset_codegen`, and `icon_codegen` are imported from an SDK
#   ENGINE_BUILTIN_ASSETS_DIR  builtin assets copied to <output>/assets/engine/
#   ENGINE_COOKED_CATALOG      cooked catalog.toml for those builtins
#   ENGINE_CMAKE_DIR           the engine source tree (cmake/web/link_flags.cmake)
# The source build also defines the target `engine_builtin_catalog`, which cooks ENGINE_COOKED_CATALOG. In
# the SDK the catalog is already cooked under bin/assets/engine/.

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
    if(WIN32 AND _engine_side AND _engine_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:engine>"
                "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copy engine.dll beside ${target}"
            VERBATIM)
    endif()

    # ENGINE_RUNTIME_ASSETS_DIR puts the target's own assets and catalog in assets/<dir>/ instead of
    # assets/. The editor uses assets/editor/, so a game module in the same bin/ keeps assets/ to itself.
    get_property(_runtime_assets_dir TARGET ${target} PROPERTY ENGINE_RUNTIME_ASSETS_DIR)
    if(_runtime_assets_dir)
        set(_engine_app_assets_out "$<TARGET_FILE_DIR:${target}>/assets/${_runtime_assets_dir}")
    else()
        set(_engine_app_assets_out "$<TARGET_FILE_DIR:${target}>/assets")
    endif()

    set(_engine_game_assets "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    if(EXISTS "${_engine_game_assets}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_engine_game_assets}"
                "${_engine_app_assets_out}"
            COMMENT "Copy game assets beside ${target}"
            VERBATIM)
    endif()

    if(_engine_side)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ENGINE_BUILTIN_ASSETS_DIR}"
                "$<TARGET_FILE_DIR:${target}>/assets/engine"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ENGINE_COOKED_CATALOG}"
                "$<TARGET_FILE_DIR:${target}>/assets/engine/catalog.toml"
            COMMENT "Copy engine assets and cooked catalog beside ${target}"
            VERBATIM)
    endif()

    get_property(_game_catalog TARGET ${target} PROPERTY ENGINE_GAME_CATALOG)
    if(_game_catalog)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_game_catalog}"
                "${_engine_app_assets_out}/catalog.toml"
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
    if(APPLE)
        # Package as a .app bundle so MACOSX_BUNDLE_ICON_FILE / MACOSX_PACKAGE_LOCATION below
        # take effect; ignored by CMake on non-Apple platforms either way.
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
        # what /PDBALTPATH:%_PDB% means; the Visual Studio generator escapes a literal % in link
        # options, so the name comes from the generator expression instead.
        target_link_options(${target} PRIVATE "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${target}>")
    elseif(WIN32 AND MSVC AND _type STREQUAL "EXECUTABLE")
        # Release-ish configs hide the console window (players shouldn't see a terminal); Debug
        # keeps it so spdlog output is visible live. /ENTRY:mainCRTStartup keeps the ordinary
        # int main(int, char**) entry point instead of requiring a WinMain — the CRT startup
        # routine mainCRTStartup already parses argv and calls main() either way, /SUBSYSTEM just
        # controls whether the OS allocates a console for the process.
        target_link_options(${target} PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>"
            "$<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>")
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
# Call after add_subdirectory(external/engine). Remaining args are sources.
# Under ENGINE_EDITOR, and always in SDK mode (WIND_EDITOR_SDK), the game is a shared module instead
# (ENGINE_GAME_MODULE, see <engine/game_entry.h>) that the editor loads on Play.
function(engine_add_game target)
    if(NOT ARGN)
        message(FATAL_ERROR "engine_add_game(${target}): pass at least one source file")
    endif()
    if(NOT TARGET engine)
        message(FATAL_ERROR "engine_add_game: add_subdirectory the Wind engine first")
    endif()
    if(NOT ENGINE_FROM_SDK AND NOT ENGINE_WITH_WINDOW)
        message(FATAL_ERROR
            "engine_add_game(${target}): ENGINE_WITH_WINDOW is OFF. "
            "It defaults ON when Wind is a subdirectory.")
    endif()

    if(ANDROID)
        add_library(${target} SHARED ${ARGN})
        set_target_properties(${target} PROPERTIES OUTPUT_NAME main)
    elseif(ENGINE_EDITOR OR ENGINE_FROM_SDK)
        # ENGINE_GAME expands to the wind_create_game / wind_destroy_game / wind_game_build_id exports.
        add_library(${target} SHARED ${ARGN})
        target_compile_definitions(${target} PRIVATE ENGINE_GAME_MODULE=1)
        set_target_properties(${target} PROPERTIES PREFIX "")
    else()
        add_executable(${target} ${ARGN})
    endif()
    engine_configure_app(${target})
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
