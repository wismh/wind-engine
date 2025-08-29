#pragma once

#include "bench_case.h"
#include "motion_view_model.h"
#include "scene.h"

#include <memory>

namespace bench {

// 200 boxes and 20 bars. The variant sheet animates the boxes' opacity and transform (paint-props), the bars' width
// (layout-props), or both. Nothing in the view-model changes.
class MotionScene final : public IScene {
public:
    explicit MotionScene(const BenchCase& bench_case);

    [[nodiscard]] engine::AssetId document() const override;
    [[nodiscard]] std::vector<engine::AssetId> stylesheets() const override;
    [[nodiscard]] std::shared_ptr<engine::ui::ViewModel> view_model() const override;
    void change_value(int step) override;
    void churn(int step) override;

private:
    BenchCase case_;
    std::shared_ptr<MotionViewModel> view_model_;
};

}
