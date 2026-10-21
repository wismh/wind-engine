#pragma once

// docs/tech/modules/Core.md

// One line per game: `ENGINE_GAME(game::Game)` in the source file that used to hold `main`.
// An exported build (no ENGINE_GAME_MODULE) gets `int main` running `Engine<GameClass>`. Android aliases
// SDL_main to it at link time (engine_add_game). The editor build compiles the game as a module with
// ENGINE_GAME_MODULE and gets the exports from <engine/core/game_module.h> instead. A static game calls
// `assets::register_game_objects` from its constructor; the module exports do that on create.

#include <engine/core/engine.h>

#if defined(ENGINE_GAME_MODULE)

#include <engine/build_id.h>
#include <engine/core/game_module.h>
#include <engine/ecs/world_serializer.h>
#include <engine/resources/assets_db.h>

#if __has_include(<component_io.h>)
#include <component_io.h>
#endif

namespace engine::detail {

inline void register_module_components(::engine::ecs::WorldSerializer& serializer) {
#if __has_include(<component_io.h>)
    ::assets::register_game_components(serializer);
#else
    (void)serializer;
#endif
}

inline void register_module_objects(::engine::AssetsDb& assets) {
#if __has_include(<component_io.h>)
    ::assets::register_game_objects(assets);
#else
    (void)assets;
#endif
}

} // namespace engine::detail

// A game module shares the C++ runtime, the heap, and STL objects with the engine.dll that loads it, so it must use
// the CRT that engine was built with (ENGINE_BUILD_DEBUG_CRT and ENGINE_BUILD_ITERATOR_DEBUG_LEVEL in
// <engine/build_id.h>). Against a Release SDK that is /MD in the game's DebugGame or Release configuration. A
// mismatch is a compile error here, not a crash on Play. docs/tech/build/CMake.md
#if defined(_MSC_VER)
#if defined(_DEBUG) && !ENGINE_BUILD_DEBUG_CRT
#error "Wind: game module has the debug CRT (/MDd, _DEBUG). Build it in DebugGame or Release against a Release SDK."
#elif !defined(_DEBUG) && ENGINE_BUILD_DEBUG_CRT
#error "Wind: the engine has the debug CRT (Debug SDK) and this game module does not. Build it in Debug."
#endif
#if _ITERATOR_DEBUG_LEVEL != ENGINE_BUILD_ITERATOR_DEBUG_LEVEL
#error "Wind: game module _ITERATOR_DEBUG_LEVEL differs from the engine. Do not set it; use DebugGame or Release."
#endif
#endif

#define ENGINE_GAME(GameClass)                                                                                         \
    static_assert(::engine::EngineGame<GameClass>,                                                                     \
                  "ENGINE_GAME needs a class derived from engine::IGame and constructible from EngineServices");       \
    ENGINE_GAME_EXPORT ::engine::IGame* wind_create_game(const ::engine::EngineServices& services) {                   \
        auto* const game = new GameClass(services);                                                                    \
        ::engine::detail::register_module_components(game->world().ctx<::engine::ecs::WorldSerializer>());             \
        ::engine::detail::register_module_objects(services.assets);                                                    \
        return game;                                                                                                   \
    }                                                                                                                  \
    ENGINE_GAME_EXPORT void wind_destroy_game(::engine::IGame* game) { delete game; }                                  \
    ENGINE_GAME_EXPORT const char* wind_game_build_id() { return ::engine::kBuildIdCStr; }                             \
    ENGINE_GAME_EXPORT void wind_register_game_components(::engine::ecs::WorldSerializer& serializer) {                \
        ::engine::detail::register_module_components(serializer);                                                      \
    }                                                                                                                  \
    ENGINE_GAME_EXPORT void wind_register_game_objects(::engine::AssetsDb& assets) {                                   \
        ::engine::detail::register_module_objects(assets);                                                             \
    }

#else

#define ENGINE_GAME(GameClass)                                                                                  \
    int main() {                                                                                                \
        ::engine::Engine<GameClass> app;                                                                        \
        if (const auto started = app.init(); !started) {                                                        \
            ::engine::report_startup_error(started.error());                                                    \
        }                                                                                                       \
        return app.run();                                                                                       \
    }

#endif
