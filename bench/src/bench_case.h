#pragma once

#include "bench_mode.h"
#include "bench_scene.h"

#include <string_view>

namespace bench {

// One row of the bench matrix: a scene, a mode, and a variant, which is empty when that pair has none.
struct BenchCase {
    BenchScene scene = BenchScene::Table;
    BenchMode mode = BenchMode::Quiet;
    std::string_view variant;

    [[nodiscard]] bool operator==(const BenchCase&) const = default;
};

}
