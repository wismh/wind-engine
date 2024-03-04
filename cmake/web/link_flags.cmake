include_guard(GLOBAL)

# Link flags for Emscripten executables (games and optional WASM tests).
# Applied to executables, not the static engine library.

function(engine_target_web_link_options target)
    if(NOT EMSCRIPTEN)
        return()
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "engine_target_web_link_options: target '${target}' does not exist")
    endif()

    set(_shell "${ENGINE_WEB_SHELL}")
    if(NOT _shell)
        set(_shell "${ENGINE_CMAKE_DIR}/cmake/web/shell.html")
    endif()
    if(NOT EXISTS "${_shell}")
        message(FATAL_ERROR "ENGINE_WEB_SHELL not found: ${_shell}")
    endif()

    set_target_properties(${target} PROPERTIES SUFFIX ".html")
    target_link_options(${target} PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMIN_WEBGL_VERSION=2"
        "SHELL:-sFULL_ES3=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sNO_EXIT_RUNTIME=1"
        "SHELL:-sEXPORTED_FUNCTIONS=_main"
        "SHELL:--shell-file=${_shell}")
endfunction()

function(engine_target_web_preload target)
    if(NOT EMSCRIPTEN)
        return()
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "engine_target_web_preload: target '${target}' does not exist")
    endif()

    if(ENGINE_BUILTIN_ASSETS_DIR)
        target_link_options(${target} PRIVATE
            "SHELL:--preload-file=${ENGINE_BUILTIN_ASSETS_DIR}@/assets/engine")
    endif()
    if(ENGINE_COOKED_CATALOG)
        target_link_options(${target} PRIVATE
            "SHELL:--preload-file=${ENGINE_COOKED_CATALOG}@/assets/engine/catalog.toml")
    endif()

    get_property(_game_assets TARGET ${target} PROPERTY ENGINE_GAME_ASSETS)
    if(_game_assets)
        target_link_options(${target} PRIVATE
            "SHELL:--preload-file=${_game_assets}@/assets")
    endif()
    get_property(_game_catalog TARGET ${target} PROPERTY ENGINE_GAME_CATALOG)
    if(_game_catalog)
        target_link_options(${target} PRIVATE
            "SHELL:--preload-file=${_game_catalog}@/assets/catalog.toml")
    endif()
endfunction()
