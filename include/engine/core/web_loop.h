#pragma once

#include <engine/core/application_state.h>
#include <engine/core/platform.h>

#include <functional>

namespace engine {

class MainLoopPolicy {
public:
    explicit MainLoopPolicy(LoopKind kind = default_loop_kind());

    [[nodiscard]] LoopKind kind() const noexcept;
    [[nodiscard]] bool uses_request_animation_frame() const noexcept;

    // Runs tick until !app.running or max_frames. Returns the number of ticks invoked.
    // Does not call Emscripten; EngineRuntime uses this policy to choose RAF vs a blocking while.
    int pump(ApplicationState& app, const std::function<void(float)>& tick, int max_frames, float dt);

private:
    LoopKind kind_{};
};

// Ordered RAF/blocking quit: on_quit then host dispose (audio + runtime shutdown).
// EngineRuntime::end_loop uses this so emscripten_set_main_loop (simulate_infinite_loop=1)
// still runs Engine::dispose even though run() never returns.
class LoopShutdown {
public:
    void complete(const std::function<void()>& on_quit, const std::function<void()>& dispose);

    [[nodiscard]] bool quit_completed() const noexcept;
    [[nodiscard]] bool dispose_completed() const noexcept;

private:
    bool quit_completed_ = false;
    bool dispose_completed_ = false;
};

}
