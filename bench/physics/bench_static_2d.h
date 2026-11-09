#pragma once

// Static-terrain scenarios (LQ-22): per-tick cost of run_physics_2d with thousands of static colliders that no
// dynamic body touches, as separate cells, compound chunks (LQ-23) or one SDF field (LQ-26).
// docs/tech/features/Physics Bench.md

#include "bench_types.h"

#include <vector>

namespace engine {
class JobSystem;
}

namespace engine::bench {

[[nodiscard]] std::vector<BenchResult> run_static_2d_benchmarks(const BenchOptions& options, JobSystem* jobs);

}
