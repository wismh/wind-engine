#include "table_scene.h"

#include "bench_assets.h"
#include "table_data.h"

#include <asset_ids.h>

#include <format>

namespace bench {
namespace {

// Churn edits rows inside this many from the top: the rows on screen at scroll 0.
constexpr int kChurnWindow = 30;

}

TableScene::TableScene(const BenchCase& bench_case)
    : case_(bench_case)
    , view_model_(std::make_shared<TableViewModel>())
    , churn_random_(kBenchSeed + 1) {
    const std::vector<TableRecord> records = make_table_records(kTableRows);
    std::vector<std::shared_ptr<TableRowViewModel>> rows;
    rows.reserve(records.size());
    for (const TableRecord& record : records) {
        rows.push_back(std::make_shared<TableRowViewModel>(record));
    }
    view_model_->rows.set(std::move(rows));
    next_id_ = static_cast<int>(kTableRows) + 1;
    view_model_->summaryText = std::format("{} rows", kTableRows);
    view_model_->frameText = std::string("frame 0");
}

engine::AssetId TableScene::document() const {
    return assets::ui::table;
}

std::vector<engine::AssetId> TableScene::stylesheets() const {
    return {assets::css::table, hover_stylesheet(case_)};
}

std::shared_ptr<engine::ui::ViewModel> TableScene::view_model() const {
    return view_model_;
}

void TableScene::change_value(int step) {
    view_model_->frameText = std::format("frame {}", step + 1);
}

void TableScene::churn(int step) {
    std::vector<std::shared_ptr<TableRowViewModel>>& rows = view_model_->rows.get();
    const auto removed = static_cast<std::ptrdiff_t>((step * 7) % kChurnWindow);
    rows.erase(rows.begin() + removed);
    const auto inserted = static_cast<std::ptrdiff_t>((step * 11) % kChurnWindow);
    const TableRecord record = make_table_record(next_id_, churn_random_);
    rows.insert(rows.begin() + inserted, std::make_shared<TableRowViewModel>(record));
    ++next_id_;
}

}
