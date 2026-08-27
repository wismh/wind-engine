#pragma once

#include "bench_case.h"
#include "bench_random.h"
#include "scene.h"
#include "table_view_model.h"

#include <memory>

namespace bench {

// 10,000 generated rows (table_data.h). One-change writes the frame counter in the toolbar. Churn removes one row and
// inserts a new one near the top every frame, so the visible window changes and the row count stays.
class TableScene final : public IScene {
public:
    explicit TableScene(const BenchCase& bench_case);

    [[nodiscard]] engine::AssetId document() const override;
    [[nodiscard]] std::vector<engine::AssetId> stylesheets() const override;
    [[nodiscard]] std::shared_ptr<engine::ui::ViewModel> view_model() const override;
    void change_value(int step) override;
    void churn(int step) override;

private:
    BenchCase case_;
    std::shared_ptr<TableViewModel> view_model_;
    BenchRandom churn_random_;
    int next_id_ = 0;
};

}
