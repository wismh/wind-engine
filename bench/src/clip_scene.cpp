#include "clip_scene.h"

#include "bench_random.h"
#include "text_data.h"

#include <asset_ids.h>

#include <utility>
#include <vector>

namespace bench {
namespace {

constexpr std::size_t kNotes = 160;
constexpr std::size_t kPaneRows = 4;
constexpr std::size_t kPanesPerRow = 3;
constexpr std::size_t kPaneLines = 30;
constexpr int kLineWords = 5;

std::vector<std::shared_ptr<ParagraphViewModel>> lines(BenchRandom& random, std::size_t count) {
    std::vector<std::shared_ptr<ParagraphViewModel>> made;
    made.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        made.push_back(std::make_shared<ParagraphViewModel>(make_paragraph(random, kLineWords)));
    }
    return made;
}

}

ClipScene::ClipScene(const BenchCase& bench_case)
    : case_(bench_case)
    , view_model_(std::make_shared<ClipViewModel>()) {
    BenchRandom random(kBenchSeed + 4);
    view_model_->notes.set(lines(random, kNotes));
    std::vector<std::shared_ptr<PaneRowViewModel>> rows;
    for (std::size_t r = 0; r < kPaneRows; ++r) {
        auto row = std::make_shared<PaneRowViewModel>();
        std::vector<std::shared_ptr<PaneViewModel>> panes;
        for (std::size_t p = 0; p < kPanesPerRow; ++p) {
            auto pane = std::make_shared<PaneViewModel>();
            pane->lines.set(lines(random, kPaneLines));
            panes.push_back(std::move(pane));
        }
        row->panes.set(std::move(panes));
        rows.push_back(std::move(row));
    }
    view_model_->paneRows.set(std::move(rows));
    view_model_->tiltLines.set(lines(random, kPaneLines));
    view_model_->innerLines.set(lines(random, kPaneLines));
}

engine::AssetId ClipScene::document() const {
    return assets::ui::clip;
}

std::vector<engine::AssetId> ClipScene::stylesheets() const {
    return {assets::css::clip};
}

std::shared_ptr<engine::ui::ViewModel> ClipScene::view_model() const {
    return view_model_;
}

void ClipScene::change_value(int) {}

void ClipScene::churn(int) {}

}
