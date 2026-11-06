#pragma once

// Private. The quiet-step counter both solvers share. Angular speed is already a scalar. Not installed.

namespace engine {

struct SleepAdvance {
    bool sleeping{false};
    int quiet{0};
};

[[nodiscard]] SleepAdvance advance_sleep(bool sleeping, int quiet, float linear, float angular, bool contacting);

[[nodiscard]] bool sleep_speed_wakes(float linear, float angular);

}
