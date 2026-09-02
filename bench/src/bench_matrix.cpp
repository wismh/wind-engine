#include "bench_matrix.h"

#include <algorithm>
#include <array>

namespace bench {
namespace {

constexpr std::array kMatrix{
        BenchCase{BenchScene::Table, BenchMode::Quiet, {}},
        BenchCase{BenchScene::Table, BenchMode::OneChange, {}},
        BenchCase{BenchScene::Table, BenchMode::Hover, kHoverPaint},
        BenchCase{BenchScene::Table, BenchMode::Hover, kHoverLayout},
        BenchCase{BenchScene::Table, BenchMode::Scroll, {}},
        BenchCase{BenchScene::Table, BenchMode::Churn, {}},
        BenchCase{BenchScene::Inspector, BenchMode::Quiet, {}},
        BenchCase{BenchScene::Inspector, BenchMode::OneChange, {}},
        BenchCase{BenchScene::Inspector, BenchMode::Hover, kHoverPaint},
        BenchCase{BenchScene::Inspector, BenchMode::Hover, kHoverLayout},
        BenchCase{BenchScene::Inspector, BenchMode::Scroll, {}},
        BenchCase{BenchScene::Inspector, BenchMode::Churn, {}},
        BenchCase{BenchScene::Hud, BenchMode::Quiet, {}},
        BenchCase{BenchScene::Hud, BenchMode::OneChange, {}},
        BenchCase{BenchScene::Hud, BenchMode::Hover, kHoverPaint},
        BenchCase{BenchScene::Hud, BenchMode::Hover, kHoverLayout},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "solid"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "rounded"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "border"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "linear-gradient"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "radial-gradient"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "conic-gradient"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "image"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "nine-slice"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "text"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "arc"},
        BenchCase{BenchScene::PaintMix, BenchMode::Quiet, "math"},
        BenchCase{BenchScene::Motion, BenchMode::Quiet, "paint-props"},
        BenchCase{BenchScene::Motion, BenchMode::Quiet, "layout-props"},
        BenchCase{BenchScene::Motion, BenchMode::Quiet, "both"},
        BenchCase{BenchScene::Text, BenchMode::Quiet, {}},
        BenchCase{BenchScene::Text, BenchMode::Scroll, {}},
        BenchCase{BenchScene::Clip, BenchMode::Quiet, {}},
        BenchCase{BenchScene::Clip, BenchMode::Scroll, {}},
};

}

std::span<const BenchCase> bench_matrix() {
    return kMatrix;
}

std::string bench_case_name(const BenchCase& bench_case) {
    std::string name(bench_scene_name(bench_case.scene));
    name += ' ';
    name += bench_mode_name(bench_case.mode);
    if (!bench_case.variant.empty()) {
        name += ' ';
        name += bench_case.variant;
    }
    return name;
}

std::optional<BenchCase> find_bench_case(BenchScene scene, BenchMode mode, std::string_view variant) {
    for (const BenchCase& bench_case : kMatrix) {
        if (bench_case.scene == scene && bench_case.mode == mode && bench_case.variant == variant) {
            return bench_case;
        }
    }
    return std::nullopt;
}

std::vector<BenchMode> bench_modes_of(BenchScene scene) {
    std::vector<BenchMode> modes;
    for (const BenchCase& bench_case : kMatrix) {
        if (bench_case.scene == scene && std::ranges::find(modes, bench_case.mode) == modes.end()) {
            modes.push_back(bench_case.mode);
        }
    }
    return modes;
}

std::vector<std::string_view> bench_variants_of(BenchScene scene, BenchMode mode) {
    std::vector<std::string_view> variants;
    for (const BenchCase& bench_case : kMatrix) {
        if (bench_case.scene == scene && bench_case.mode == mode && !bench_case.variant.empty()) {
            variants.push_back(bench_case.variant);
        }
    }
    return variants;
}

}
