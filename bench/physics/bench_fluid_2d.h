#pragma once

// Fluid scenarios (LQ-27): per-tick cost of run_fluid_2d for a dam break in an SdfCollider2D tank.
// docs/tech/features/Physics Bench.md

#include "bench_types.h"

#include <vector>

namespace engine {
class JobSystem;
}

namespace engine::bench {

[[nodiscard]] std::vector<BenchResult> run_fluid_2d_benchmarks(const BenchOptions& options, JobSystem* jobs);

}
