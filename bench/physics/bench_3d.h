#pragma once

#include "bench_types.h"

#include <vector>

namespace engine::bench {

[[nodiscard]] std::vector<BenchResult> run_all_3d_benchmarks(int steps_override = 0);

}
