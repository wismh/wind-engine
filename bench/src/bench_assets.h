#pragma once

#include "bench_case.h"

#include <engine/resources/asset_id.h>

namespace bench {

// The hover sheet merged after a reference scene's own: hover_layout.css in hover/layout, hover_paint.css otherwise.
[[nodiscard]] engine::AssetId hover_stylesheet(const BenchCase& bench_case);

}
