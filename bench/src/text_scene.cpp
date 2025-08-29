#include "text_scene.h"

#include "text_data.h"

#include <asset_ids.h>

#include <string>
#include <utility>
#include <vector>

namespace bench {
namespace {

constexpr std::size_t kParagraphsPerSize = 20;
constexpr int kParagraphWords = 70;
constexpr std::size_t kLines = 200;
constexpr std::string_view kLine = "The same row of text, measured and drawn again on every line of this list.";

std::vector<std::shared_ptr<ParagraphViewModel>> paragraph_rows(const std::vector<std::string>& texts) {
    std::vector<std::shared_ptr<ParagraphViewModel>> rows;
    rows.reserve(texts.size());
    for (const std::string& text : texts) {
        rows.push_back(std::make_shared<ParagraphViewModel>(text));
    }
    return rows;
}

}

TextScene::TextScene(const BenchCase& bench_case)
    : case_(bench_case)
    , view_model_(std::make_shared<TextViewModel>()) {
    const std::vector<std::string> texts = make_paragraphs(kParagraphsPerSize * 3, kParagraphWords);
    view_model_->large.set(paragraph_rows({texts.begin(), texts.begin() + kParagraphsPerSize}));
    view_model_->medium.set(
            paragraph_rows({texts.begin() + kParagraphsPerSize, texts.begin() + kParagraphsPerSize * 2}));
    view_model_->small.set(paragraph_rows({texts.begin() + kParagraphsPerSize * 2, texts.end()}));
    view_model_->lines.set(paragraph_rows(std::vector<std::string>(kLines, std::string(kLine))));
}

engine::AssetId TextScene::document() const {
    return assets::ui::text;
}

std::vector<engine::AssetId> TextScene::stylesheets() const {
    return {assets::css::text};
}

std::shared_ptr<engine::ui::ViewModel> TextScene::view_model() const {
    return view_model_;
}

void TextScene::change_value(int) {}

void TextScene::churn(int) {}

}
