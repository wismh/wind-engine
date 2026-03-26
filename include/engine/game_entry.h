#pragma once

// docs/tech/modules/Core.md

// One line per game: `ENGINE_GAME(game::Game)` in the source file that used to hold `main`.
// An exported build (no ENGINE_GAME_MODULE) gets `int main` running `Engine<GameClass>`. Android aliases
// SDL_main to it at link time (engine_add_game). The editor build compiles the game as a module with
// ENGINE_GAME_MODULE and gets the three exports from <engine/core/game_module.h> instead.

#include <engine/core/engine.h>

#if defined(ENGINE_GAME_MODULE)

#include <engine/build_id.h>
#include <engine/core/game_module.h>

#define ENGINE_GAME(GameClass)                                                                                  \
    static_assert(::engine::EngineGame<GameClass>,                                                             \
            "ENGINE_GAME needs a class derived from engine::IGame and constructible from EngineServices");      \
    ENGINE_GAME_EXPORT ::engine::IGame* wind_create_game(const ::engine::EngineServices& services) {              \
        return new GameClass(services);                                                                         \
    }                                                                                                           \
    ENGINE_GAME_EXPORT void wind_destroy_game(::engine::IGame* game) {                                          \
        delete game;                                                                                            \
    }                                                                                                           \
    ENGINE_GAME_EXPORT const char* wind_game_build_id() {                                                       \
        return ::engine::kBuildIdCStr;                                                                          \
    }

#else

#define ENGINE_GAME(GameClass)                                                                                  \
    int main() {                                                                                                \
        ::engine::Engine<GameClass> app;                                                                        \
        if (!app.init()) {                                                                                      \
            return 1;                                                                                           \
        }                                                                                                       \
        return app.run();                                                                                       \
    }

#endif
