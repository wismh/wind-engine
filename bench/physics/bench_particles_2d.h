#pragma once

// Particle collision scenarios (LQ-25): per-frame cost of run_particles with emitters colliding against nothing, a
// box floor, or SdfCollider2D terrain. docs/tech/features/Physics Bench.md

#include "bench_types.h"

#include <vector>

namespace engine {
class JobSystem;
}

namespace engine::bench {

[[nodiscard]] std::vector<BenchResult> run_particle_2d_benchmarks(const BenchOptions& options, JobSystem* jobs);

}
