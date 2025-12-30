#pragma once

#include "core/presentation.h"

#include <engine/core/fixed_step.h>
#include <engine/core/web_loop.h>

#include <chrono>
#include <functional>
#include <memory>

namespace engine {

class IAudioSystem;
class IGame;
class InputSystem;

// Frame clock and present order. Knows IPresentation, not SDL or OpenGL.
class GameLoop {
public:
    [[nodiscard]] int run(IPresentation& presentation, IGame& game, InputSystem& input, IAudioSystem* audio,
            std::function<void()> host_dispose);

private:
    void begin();
    void tick();
    void reentrant_tick();
    void end();
    [[nodiscard]] float consume_dt();
    static void main_loop_thunk(void* self);

    IPresentation* presentation_ = nullptr;
    IGame* game_ = nullptr;
    InputSystem* input_ = nullptr;
    IAudioSystem* audio_ = nullptr;
    std::unique_ptr<FixedStepClock> clock_;
    std::chrono::steady_clock::time_point last_{};
    std::function<void()> host_dispose_;
    LoopShutdown shutdown_;
};

}
