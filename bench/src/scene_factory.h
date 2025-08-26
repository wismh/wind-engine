#pragma once

#include "bench_case.h"
#include "scene.h"

#include <memory>

namespace bench {

// The scene of a matrix row, with its data generated.
[[nodiscard]] std::unique_ptr<IScene> make_scene(const BenchCase& bench_case);

}
