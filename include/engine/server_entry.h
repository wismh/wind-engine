#pragma once

// docs/tech/modules/Core.md

// One line per dedicated server: `ENGINE_GAME_SERVER(game::Server)` in the server target's own source file. It gets
// `int main` running `Server<GameClass>`: no window, no render, UI, or audio systems, a fixed-step loop that sleeps to
// the tick rate, logging to stdout, and a clean stop on Ctrl+C or SIGTERM. Selected by the target
// (`engine_add_server`), never by ENGINE_EDITOR. A server is an executable, not a module the editor loads.

#if defined(ENGINE_GAME_MODULE)
#error "<engine/server_entry.h> is for an engine_add_server target. A game module uses <engine/game_entry.h>."
#endif

#include <engine/core/server.h>

#define ENGINE_GAME_SERVER(GameClass)                                                                                  \
    int main(int argc, char** argv) {                                                                                  \
        return ::engine::run_server<GameClass>(argc, argv);                                                            \
    }
