#pragma once

#include <cstdint>

namespace bench {

// What the bench does on one tick of its frame system (BenchSchedule::at).
enum class BenchStep : std::uint8_t {
    // The document is not laid out yet.
    Wait,
    // Look up the rest point, the wheel point, and the hover targets in the laid-out tree; rest the pointer.
    Setup,
    // Drive the mode; nothing is kept.
    Warmup,
    // Empty the profiler rings, then drive the mode. This tick is the first measured one.
    Clear,
    // Drive the mode.
    Measure,
    // The rings hold every measured frame: write the report and quit.
    Finish,
    // The frames never arrived: quit with an error.
    Fail,
};

}
