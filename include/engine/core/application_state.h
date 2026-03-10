#pragma once

// docs/tech/modules/Core.md

namespace engine {

struct ApplicationState {
    bool running = true;
    bool paused = false;

    void quit() {
        running = false;
    }
};

}
