#pragma once

// docs/tech/modules/Core.md

#include <engine/core/application_state.h>
#include <engine/core/time.h>

namespace engine {

inline constexpr bool kClockStepping = true;

class FixedStepClock {
public:
    // `stepping` is read on every advance. A world passes its own flag; the default stays true.
    FixedStepClock(Time& time, const ApplicationState& app_state, const bool& stepping = kClockStepping);

    // Clamps wall dt. Runs 0..kMaxFixedSteps when the process is not paused and `stepping` is true.
    // Leftover accumulator is discarded if the cap is hit. Does not run systems.
    [[nodiscard]] int advance(float real_dt);

private:
    Time* time_ = nullptr;
    const ApplicationState* app_state_ = nullptr;
    const bool* stepping_ = &kClockStepping;
};

}
