#pragma once

#include <engine/core/application_state.h>
#include <engine/core/time.h>

namespace engine {

class FixedStepClock {
public:
    FixedStepClock(Time& time, const ApplicationState& app_state);

    // Clamps wall dt, runs 0..kMaxFixedSteps sim ticks when not paused, returns the step count.
    // Does not call IGame. Leftover accumulator is discarded if the cap is hit.
    [[nodiscard]] int advance(float real_dt);

private:
    Time* time_ = nullptr;
    const ApplicationState* app_state_ = nullptr;
};

}
