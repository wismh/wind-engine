#include "inspector_scene.h"

#include "bench_assets.h"
#include "bench_random.h"

#include <asset_ids.h>

#include <array>
#include <format>
#include <string_view>

namespace bench {
namespace {

constexpr int kSections = 22;
constexpr int kGroups = 4;
constexpr int kTextRows = 4;
constexpr int kNumberRows = 3;
constexpr int kBoolRows = 3;

constexpr std::array<std::string_view, 8> kSectionNames{
        "Transform", "Sprite", "Collider", "Rigidbody", "Animator", "Audio Source", "Particles", "Script",
};

constexpr std::array<std::string_view, 12> kPropertyNames{
        "Name", "Tag", "Layer", "Material", "Offset", "Size", "Mass", "Speed", "Range", "Delay", "Volume", "Pivot",
};

std::string property_name(int index) {
    const std::string_view base = kPropertyNames[static_cast<std::size_t>(index) % kPropertyNames.size()];
    return std::format("{} {}", base, index / static_cast<int>(kPropertyNames.size()) + 1);
}

}

InspectorScene::InspectorScene(const BenchCase& bench_case)
    : case_(bench_case)
    , view_model_(std::make_shared<InspectorViewModel>()) {
    BenchRandom random(kBenchSeed + 2);
    std::vector<std::shared_ptr<SectionViewModel>> sections;
    for (int s = 0; s < kSections; ++s) {
        const std::string_view section_name = kSectionNames[static_cast<std::size_t>(s) % kSectionNames.size()];
        auto section = std::make_shared<SectionViewModel>(std::format("{} {}", section_name, s + 1));
        std::vector<std::shared_ptr<GroupViewModel>> groups;
        int property = 0;
        for (int g = 0; g < kGroups; ++g) {
            auto group = std::make_shared<GroupViewModel>(std::format("Group {}", g + 1));
            std::vector<std::shared_ptr<TextRowViewModel>> text_rows;
            for (int r = 0; r < kTextRows; ++r) {
                text_rows.push_back(std::make_shared<TextRowViewModel>(property_name(property++),
                        std::format("value_{}", random.between(100, 999))));
            }
            std::vector<std::shared_ptr<NumberRowViewModel>> number_rows;
            for (int r = 0; r < kNumberRows; ++r) {
                number_rows.push_back(
                        std::make_shared<NumberRowViewModel>(property_name(property++), random.between(0, 1000)));
            }
            std::vector<std::shared_ptr<BoolRowViewModel>> bool_rows;
            for (int r = 0; r < kBoolRows; ++r) {
                bool_rows.push_back(
                        std::make_shared<BoolRowViewModel>(property_name(property++), random.between(0, 1) == 1));
            }
            group->textRows.set(std::move(text_rows));
            group->numberRows.set(std::move(number_rows));
            group->boolRows.set(std::move(bool_rows));
            groups.push_back(std::move(group));
        }
        section->groups.set(std::move(groups));
        sections.push_back(std::move(section));
    }
    view_model_->sections.set(std::move(sections));
    view_model_->frameText = std::string("frame 0");
}

engine::AssetId InspectorScene::document() const {
    return assets::ui::inspector;
}

std::vector<engine::AssetId> InspectorScene::stylesheets() const {
    return {assets::css::inspector, hover_stylesheet(case_)};
}

std::shared_ptr<engine::ui::ViewModel> InspectorScene::view_model() const {
    return view_model_;
}

void InspectorScene::change_value(int step) {
    view_model_->frameText = std::format("frame {}", step + 1);
}

void InspectorScene::churn(int step) {
    // A section near the top of the panel, so the edit is on screen: section 0 on steps 0 and 1, section 1 on steps
    // 2 and 3, then again.
    const std::vector<std::shared_ptr<SectionViewModel>>& sections = view_model_->sections.get();
    const std::size_t index = static_cast<std::size_t>(step / 2) % 2;
    sections[index]->set_collapsed(step % 2 == 0);
}

}
