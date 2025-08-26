#include "bench_schedule.h"

namespace bench {

BenchStep BenchSchedule::at(int tick, int stored) const {
    if (tick < kSetupTick) {
        return BenchStep::Wait;
    }
    if (tick == kSetupTick) {
        return BenchStep::Setup;
    }
    if (tick < warmup) {
        return BenchStep::Warmup;
    }
    if (tick == warmup) {
        return BenchStep::Clear;
    }
    if (stored >= frames) {
        return BenchStep::Finish;
    }
    if (tick > warmup + frames + kGraceTicks) {
        return BenchStep::Fail;
    }
    return BenchStep::Measure;
}

}
