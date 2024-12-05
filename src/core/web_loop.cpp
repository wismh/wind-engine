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

void LoopShutdown::complete(const std::function<void()>& on_quit, const std::function<void()>& dispose) {
    if (!quit_completed_) {
        quit_completed_ = true;
        if (on_quit) {
            on_quit();
        }
    }
    if (!dispose_completed_) {
        dispose_completed_ = true;
        if (dispose) {
            dispose();
        }
    }
}

bool LoopShutdown::quit_completed() const noexcept {
    return quit_completed_;
}

bool LoopShutdown::dispose_completed() const noexcept {
    return dispose_completed_;
}

}
