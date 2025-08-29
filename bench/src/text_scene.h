#pragma once

#include "bench_case.h"
#include "scene.h"
#include "text_view_model.h"

#include <memory>

namespace bench {

// Wrapped paragraphs at 22, 16, and 12 px, then 200 identical lines, in one ScrollView. Nothing in the view-model
// changes; the scroll mode turns the wheel over it.
class TextScene final : public IScene {
public:
    explicit TextScene(const BenchCase& bench_case);

    [[nodiscard]] engine::AssetId document() const override;
    [[nodiscard]] std::vector<engine::AssetId> stylesheets() const override;
    [[nodiscard]] std::shared_ptr<engine::ui::ViewModel> view_model() const override;
    void change_value(int step) override;
    void churn(int step) override;

private:
    BenchCase case_;
    std::shared_ptr<TextViewModel> view_model_;
};

}
