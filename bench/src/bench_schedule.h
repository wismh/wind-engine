#pragma once

#include "bench_step.h"

namespace bench {

// The tick that looks the laid-out scene up. Bind builds the tree on tick 0 and paint lays it out; a virtualized list
// swaps its rows for the visible window on the next frame, and a formula is laid out again once its font is in. By
// tick 3 the tree is the one the measured frames see.
inline constexpr int kSetupTick = 3;
// The first tick the mode drives.
inline constexpr int kFirstDrivenTick = kSetupTick + 1;
// Ticks past the expected end before the bench gives up on the measured frames.
inline constexpr int kGraceTicks = 600;

// The measurement protocol over the ticks of the bench's frame system, which runs in Phase::Game. A tick's profiler
// frame is committed at the start of the next tick, so the frames of ticks `warmup` .. `warmup + frames - 1` are in
// the rings when tick `warmup + frames` runs.
struct BenchSchedule {
    int warmup = 0;
    int frames = 0;

    // `stored` is how many frames the scene canvas's ring holds now.
    [[nodiscard]] BenchStep at(int tick, int stored) const;

    // The mode's step number of a driven tick: 0 on kFirstDrivenTick.
    [[nodiscard]] static int mode_step(int tick) { return tick - kFirstDrivenTick; }
};

}
