#include <engine/core/fixed_step.h>

#include <algorithm>

namespace engine {

FixedStepClock::FixedStepClock(Time& time, const ApplicationState& app_state)
    : time_(&time)
    , app_state_(&app_state) {}

int FixedStepClock::advance(float real_dt) {
    const float frame_dt = std::clamp(real_dt, 0.0f, MAX_FRAME_DT);

    int steps = 0;
    if (!app_state_->paused) {
        time_->accumulator += frame_dt;
        while (time_->accumulator >= FIXED && steps < MAX_FIXED_STEPS) {
            time_->fixedDeltaTime = FIXED;
            time_->accumulator -= FIXED;
            ++steps;
        }
        if (steps >= MAX_FIXED_STEPS) {
            time_->accumulator = 0.0f;
        }
    }

    time_->deltaTime = frame_dt;
    time_->alpha = time_->accumulator / FIXED;
    return steps;
}

}
