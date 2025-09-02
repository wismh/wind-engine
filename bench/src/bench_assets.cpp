#include "bench_assets.h"

#include "bench_matrix.h"

#include <asset_ids.h>

namespace bench {

engine::AssetId hover_stylesheet(const BenchCase& bench_case) {
    if (bench_case.mode == BenchMode::Hover && bench_case.variant == kHoverLayout) {
        return assets::css::hover_layout;
    }
    return assets::css::hover_paint;
}

}
