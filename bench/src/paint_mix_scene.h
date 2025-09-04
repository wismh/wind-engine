#pragma once

#include "bench_case.h"
#include "paint_mix_view_model.h"
#include "scene.h"

#include <memory>

namespace bench {

// 1,000 equal cells (25 rows of 40), all one painter call kind picked by the variant. Box kinds share
// paint_mix.xml with a mix_*.css sheet; text, math, and arc have their own document. Only the quiet mode runs it.
class PaintMixScene final : public IScene {
public:
    explicit PaintMixScene(const BenchCase& bench_case);

    [[nodiscard]] engine::AssetId document() const override;
    [[nodiscard]] std::vector<engine::AssetId> stylesheets() const override;
    [[nodiscard]] std::shared_ptr<engine::ui::ViewModel> view_model() const override;
    void change_value(int step) override;
    void churn(int step) override;

private:
    BenchCase case_;
    std::shared_ptr<PaintMixViewModel> view_model_;
};

}
