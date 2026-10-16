#include "inspector_panel.h"

#include "asset_inspection.h"
#include "entity_inspection.h"

#include <engine/ecs/world.h>
#include <engine/ui/inspector.h>

#include <filesystem>
#include <iterator>
#include <string_view>
#include <utility>
#include <variant>

namespace editor {
namespace {

constexpr char kNothingSelected[] = "Nothing selected";
constexpr char kSelectHint[] = "Select a file in Project, an entity in Hierarchy, or an element in UI Tree.";
constexpr char kUiElement[] = "UI element";
constexpr char kEntity[] = "Entity";

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

}

InspectorPanel::InspectorPanel(const EditorSelection& selection)
    : selection_(&selection), view_model_(std::make_shared<InspectorViewModel>()) {
    show_nothing(kNothingSelected);
}

const std::shared_ptr<InspectorViewModel>& InspectorPanel::view_model() const {
    return view_model_;
}

void InspectorPanel::attach(engine::ecs::World& game) {
    game_ = &game;
}

void InspectorPanel::detach() {
    game_ = nullptr;
    const SelectionTarget& target = selection_->target();
    if (std::holds_alternative<UiElementSelection>(target)) {
        show_nothing(kNothingSelected);
    } else if (const auto* entity_sel = std::get_if<EntitySelection>(&target)) {
        if (edit_world_ == nullptr || !edit_world_->valid(entity_sel->entity)) {
            show_nothing(kNothingSelected);
        }
    } else if (!std::holds_alternative<AssetSelection>(target)) {
        show_nothing(kNothingSelected);
    }
}

bool InspectorPanel::attached() const {
    return game_ != nullptr;
}

void InspectorPanel::set_edit_world(engine::ecs::World* edit_world) {
    edit_world_ = edit_world;
}

void InspectorPanel::refresh() {
    const SelectionTarget& target = selection_->target();
    if (const auto* asset = std::get_if<AssetSelection>(&target)) {
        if (asset_revision_ != selection_->revision()) {
            asset_revision_ = selection_->revision();
            show_asset(*asset);
        }
        return;
    }
    asset_revision_.reset();
    if (const auto* element = std::get_if<UiElementSelection>(&target)) {
        if (game_ != nullptr) {
            show_ui_element(*element);
        } else {
            show_nothing(kNothingSelected);
        }
        return;
    }
    if (const auto* entity = std::get_if<EntitySelection>(&target)) {
        const engine::ecs::World* active = game_ != nullptr ? game_ : edit_world_;
        if (active != nullptr && active->valid(entity->entity)) {
            show_entity(*active, entity->entity);
        } else {
            show_nothing(kNothingSelected);
        }
        return;
    }
    show_nothing(kNothingSelected);
}

void InspectorPanel::toggle_section(const std::string& heading) {
    if (collapsed_.erase(heading) == 0) {
        collapsed_.insert(heading);
    }
    show_sections(std::move(content_));
}

void InspectorPanel::show_nothing(std::string title) {
    view_model_->title = std::move(title);
    view_model_->subtitle = std::string(kSelectHint);
    show_sections({});
}

void InspectorPanel::show_asset(const AssetSelection& asset) {
    view_model_->title = path_text(asset.path.filename());
    view_model_->subtitle = std::string(asset.directory ? "Folder" : "File");
    show_sections(inspect_asset(asset));
}

void InspectorPanel::show_ui_element(const UiElementSelection& element) {
    const engine::ui::InspectorPick pick = engine::ui::inspector_selection(*game_, element.window);
    std::vector<std::string> detail = split_lines(engine::ui::inspector_detail(*game_, pick));
    if (detail.size() < 2) {
        // Nothing selected, or the element left the live tree: one line that says so.
        show_nothing(detail.empty() ? std::string(kNothingSelected) : std::move(detail.front()));
        view_model_->subtitle = std::string(kUiElement);
        return;
    }
    view_model_->title = std::move(detail.front());
    view_model_->subtitle = std::string(kUiElement);
    std::vector<InspectorSection> content;
    content.push_back(InspectorSection{.heading = "Computed",
            .lines = std::vector<std::string>(std::make_move_iterator(detail.begin() + 1),
                    std::make_move_iterator(detail.end()))});
    InspectorSection rules{.heading = "Rules"};
    for (const std::string& rule : engine::ui::inspector_rules(*game_, pick)) {
        for (std::string& line : split_lines(rule)) {
            rules.lines.push_back(std::move(line));
        }
    }
    if (rules.lines.empty()) {
        rules.lines.emplace_back("No rule matches.");
    }
    content.push_back(std::move(rules));
    show_sections(std::move(content));
}

void InspectorPanel::show_entity(const engine::ecs::World& world, engine::ecs::Entity entity) {
    view_model_->title = entity_display_name(world, entity);
    view_model_->subtitle = std::string(kEntity);
    show_sections(inspect_entity(world, entity));
}

void InspectorPanel::show_sections(std::vector<InspectorSection> content) {
    content_ = std::move(content);
    sections_.resize(content_.size());
    lines_.resize(content_.size());
    for (std::size_t i = 0; i < content_.size(); ++i) {
        const InspectorSection& section = content_[i];
        if (!sections_[i]) {
            sections_[i] = std::make_shared<InspectorSectionViewModel>(*this);
        }
        const bool expanded = !collapsed_.contains(section.heading);
        InspectorSectionViewModel& view = *sections_[i];
        view.heading = section.heading;
        view.expanded = expanded;
        std::vector<std::shared_ptr<InspectorLineViewModel>>& lines = lines_[i];
        lines.resize(expanded ? section.lines.size() : 0);
        for (std::size_t j = 0; j < lines.size(); ++j) {
            if (!lines[j]) {
                lines[j] = std::make_shared<InspectorLineViewModel>();
            }
            lines[j]->text = section.lines[j];
        }
        view.lines.set(lines);
    }
    view_model_->sections.set(sections_);
}

}
