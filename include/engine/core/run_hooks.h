#pragma once

// docs/tech/architecture/Runtime Loop.md

#include <engine/core/cli_commands.h>

#include <functional>

namespace engine {

// What the game loop calls besides the engine frame. `Engine<GameT>` fills on_start and on_quit from
// the game. The editor also uses on_frame_end and cli. An empty function is skipped.
struct RunHooks {
    // After the loop attaches to the presentation, before the first frame.
    std::function<void()> on_start;
    // After every full frame (`GameLoop::tick`). Not called from the reentrant modal-loop frame.
    std::function<void()> on_frame_end;
    // Once, when the loop ends, before the host dispose callback.
    std::function<void()> on_quit;
    // The host's `wind-cli` commands and descriptor kind. A game leaves it empty.
    CliCommands cli;
};

}
