#include "motion_scene.h"

#include <asset_ids.h>

#include <utility>

namespace bench {
namespace {

constexpr std::size_t kBoxRows = 10;
constexpr std::size_t kBoxesPerRow = 20;
constexpr std::size_t kBars = 20;

}

MotionScene::MotionScene(const BenchCase& bench_case)
    : case_(bench_case)
    , view_model_(std::make_shared<MotionViewModel>()) {
    std::vector<std::shared_ptr<CellRowViewModel>> rows;
    for (std::size_t i = 0; i < kBoxRows; ++i) {
        rows.push_back(std::make_shared<CellRowViewModel>(kBoxesPerRow));
    }
    view_model_->rows.set(std::move(rows));
    std::vector<std::shared_ptr<CellViewModel>> bars;
    for (std::size_t i = 0; i < kBars; ++i) {
        bars.push_back(std::make_shared<CellViewModel>());
    }
    view_model_->bars.set(std::move(bars));
}

engine::AssetId MotionScene::document() const {
    return assets::ui::motion;
}

std::vector<engine::AssetId> MotionScene::stylesheets() const {
    if (case_.variant == "paint-props") {
        return {assets::css::motion, assets::css::motion_paint};
    }
    if (case_.variant == "layout-props") {
        return {assets::css::motion, assets::css::motion_layout};
    }
    return {assets::css::motion, assets::css::motion_both};
}

std::shared_ptr<engine::ui::ViewModel> MotionScene::view_model() const {
    return view_model_;
}

void MotionScene::change_value(int) {}

void MotionScene::churn(int) {}

}
