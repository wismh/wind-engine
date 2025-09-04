#pragma once

#include "bench_case.h"
#include "clip_view_model.h"
#include "scene.h"

#include <memory>

namespace bench {

// Nested ScrollViews: an outer one, twelve panes inside it, and a rotated block holding a pane with one more pane in
// it. Nothing in the view-model changes; the scroll mode turns the wheel over the notes column of the outer one.
class ClipScene final : public IScene {
public:
    explicit ClipScene(const BenchCase& bench_case);

    [[nodiscard]] engine::AssetId document() const override;
    [[nodiscard]] std::vector<engine::AssetId> stylesheets() const override;
    [[nodiscard]] std::shared_ptr<engine::ui::ViewModel> view_model() const override;
    void change_value(int step) override;
    void churn(int step) override;

private:
    BenchCase case_;
    std::shared_ptr<ClipViewModel> view_model_;
};

}
