#pragma once

namespace engine {

struct ApplicationState {
    bool running = true;
    bool paused = false;

    void quit() {
        running = false;
    }
};

}
