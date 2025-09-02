#include "paint_mix_scene.h"

#include <asset_ids.h>

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace bench {
namespace {

constexpr std::size_t kRows = 25;
constexpr std::size_t kColumns = 40;

struct BoxVariant {
    std::string_view name;
    engine::AssetId sheet;
};

constexpr std::array kBoxVariants{
        BoxVariant{"solid", assets::css::mix_solid},
        BoxVariant{"rounded", assets::css::mix_rounded},
        BoxVariant{"border", assets::css::mix_border},
        BoxVariant{"linear-gradient", assets::css::mix_linear},
        BoxVariant{"radial-gradient", assets::css::mix_radial},
        BoxVariant{"conic-gradient", assets::css::mix_conic},
        BoxVariant{"image", assets::css::mix_image},
        BoxVariant{"nine-slice", assets::css::mix_nine_slice},
};

std::optional<engine::AssetId> box_sheet(std::string_view variant) {
    for (const BoxVariant& box : kBoxVariants) {
        if (box.name == variant) {
            return box.sheet;
        }
    }
    return std::nullopt;
}

}

PaintMixScene::PaintMixScene(const BenchCase& bench_case)
    : case_(bench_case)
    , view_model_(std::make_shared<PaintMixViewModel>()) {
    std::vector<std::shared_ptr<CellRowViewModel>> rows;
    rows.reserve(kRows);
    for (std::size_t i = 0; i < kRows; ++i) {
        rows.push_back(std::make_shared<CellRowViewModel>(kColumns));
    }
    view_model_->rows.set(std::move(rows));
}

engine::AssetId PaintMixScene::document() const {
    if (case_.variant == "text") {
        return assets::ui::paint_mix_text;
    }
    if (case_.variant == "math") {
        return assets::ui::paint_mix_math;
    }
    if (case_.variant == "arc") {
        return assets::ui::paint_mix_arc;
    }
    return assets::ui::paint_mix;
}

std::vector<engine::AssetId> PaintMixScene::stylesheets() const {
    if (const std::optional<engine::AssetId> sheet = box_sheet(case_.variant)) {
        return {assets::css::paint_mix, *sheet};
    }
    return {assets::css::paint_mix};
}

std::shared_ptr<engine::ui::ViewModel> PaintMixScene::view_model() const {
    return view_model_;
}

void PaintMixScene::change_value(int) {}

void PaintMixScene::churn(int) {}

}
