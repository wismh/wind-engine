#pragma once

#include "bench_case.h"
#include "inspector_view_model.h"
#include "scene.h"

#include <memory>

namespace bench {

// Sections of property groups, every one expanded, nothing virtualized. One-change writes the frame counter in the
// toolbar. Churn collapses a section on even steps and expands it again on the odd step after.
class InspectorScene final : public IScene {
public:
    explicit InspectorScene(const BenchCase& bench_case);

    [[nodiscard]] engine::AssetId document() const override;
    [[nodiscard]] std::vector<engine::AssetId> stylesheets() const override;
    [[nodiscard]] std::shared_ptr<engine::ui::ViewModel> view_model() const override;
    void change_value(int step) override;
    void churn(int step) override;

private:
    BenchCase case_;
    std::shared_ptr<InspectorViewModel> view_model_;
};

}
