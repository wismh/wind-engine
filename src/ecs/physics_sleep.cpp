#include "physics_sleep.h"

#include <engine/ecs/physics.h>

#include <algorithm>

namespace engine {

bool sleep_speed_wakes(float linear, float angular) {
    return linear >= kSleepLinear || angular >= kSleepAngular;
}

SleepAdvance advance_sleep(bool sleeping, int quiet, float linear, float angular, bool contacting) {
    if (linear < kSleepLinear && angular < kSleepAngular && contacting) {
        quiet = std::min(quiet + 1, kSleepSteps);
        if (quiet >= kSleepSteps) {
            sleeping = true;
        }
    } else {
        quiet = 0;
        if (sleep_speed_wakes(linear, angular)) {
            sleeping = false;
        }
    }
    return SleepAdvance{sleeping, quiet};
}

}
