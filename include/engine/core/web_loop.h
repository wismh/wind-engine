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

}
