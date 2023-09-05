#include <engine/core/fixed_step.h>

#include <algorithm>

namespace engine {

FixedStepClock::FixedStepClock(Time& time, const ApplicationState& app_state)
    : time_(&time)
    , app_state_(&app_state) {}

int FixedStepClock::advance(float real_dt) {
    const float frame_dt = std::clamp(real_dt, 0.0f, kMaxFrameDt);

    int steps = 0;
    if (!app_state_->paused) {
        time_->accumulator += frame_dt;
        while (time_->accumulator >= kFixed && steps < kMaxFixedSteps) {
            time_->fixed_delta_time = kFixed;
            time_->accumulator -= kFixed;
            ++steps;
        }
        if (steps >= kMaxFixedSteps) {
            time_->accumulator = 0.0f;
        }
    }

    time_->delta_time = frame_dt;
    time_->alpha = time_->accumulator / kFixed;
    return steps;
}

}
