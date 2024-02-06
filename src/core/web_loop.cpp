#include <engine/core/web_loop.h>

namespace engine {

MainLoopPolicy::MainLoopPolicy(LoopKind kind) : kind_(kind) {}

LoopKind MainLoopPolicy::kind() const noexcept {
    return kind_;
}

bool MainLoopPolicy::uses_request_animation_frame() const noexcept {
    return kind_ == LoopKind::RequestAnimationFrame;
}

int MainLoopPolicy::pump(ApplicationState& app, const std::function<void(float)>& tick, int max_frames, float dt) {
    if (tick == nullptr || max_frames <= 0) {
        return 0;
    }
    int frames = 0;
    while (app.running && frames < max_frames) {
        tick(dt);
        ++frames;
    }
    return frames;
}

}
