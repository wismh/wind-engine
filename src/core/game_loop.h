#pragma once

#include "core/presentation.h"

#include <engine/core/run_hooks.h>
#include <engine/core/web_loop.h>
#include <engine/core/worlds.h>

#include <chrono>
#include <functional>

namespace engine {

class HttpClient;
class IAudioSystem;
class InputSystem;
class ProcessLauncher;

// Frame clock and present order. Knows IPresentation, not SDL or OpenGL.
class GameLoop {
public:
    [[nodiscard]] int run(IPresentation& presentation, RunHooks hooks, Worlds& worlds, InputSystem& input,
            IAudioSystem* audio, HttpClient* http, ProcessLauncher* processes, std::function<void()> host_dispose);

private:
    void begin();
    void tick();
    void reentrant_tick();
    void end();
    [[nodiscard]] float consume_dt();
    static void main_loop_thunk(void* self);

    IPresentation* presentation_ = nullptr;
    RunHooks hooks_;
    Worlds* worlds_ = nullptr;
    InputSystem* input_ = nullptr;
    IAudioSystem* audio_ = nullptr;
    HttpClient* http_ = nullptr;
    ProcessLauncher* processes_ = nullptr;
    std::chrono::steady_clock::time_point last_{};
    std::function<void()> host_dispose_;
    LoopShutdown shutdown_;
};

}
