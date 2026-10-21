#include "inspector_panel.h"

#include "asset_inspection.h"
#include "entity_edit.h"
#include "entity_inspection.h"

#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/ui/inspector.h>

#include <filesystem>
#include <format>
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
    view_model_->toggleAdd.bind_to<InspectorPanel, &InspectorPanel::toggle_add>(*this);
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

void InspectorPanel::set_assets(engine::AssetsDb* assets) {
    assets_ = assets;
}

void InspectorPanel::set_journal(CommandHistory* journal, std::function<void()> on_history) {
    journal_ = journal;
    on_history_ = std::move(on_history);
}

void InspectorPanel::end_field_edit() {
    editing_key_.clear();
}

void InspectorPanel::refresh() {
    const SelectionTarget& target = selection_->target();
    const EntitySelection* entity_now = std::get_if<EntitySelection>(&target);
    const engine::ecs::Entity current = entity_now != nullptr ? entity_now->entity : engine::ecs::Entity{};
    if (current != editing_entity_) {
        editing_key_.clear();
        editing_entity_ = current;
    }
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
    show_add(nullptr, {}, false);
}

void InspectorPanel::show_asset(const AssetSelection& asset) {
    view_model_->title = path_text(asset.path.filename());
    view_model_->subtitle = std::string(asset.directory ? "Folder" : "File");
    show_sections(inspect_asset(asset));
    show_add(nullptr, {}, false);
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
    show_add(nullptr, {}, false);
}

void InspectorPanel::show_entity(const engine::ecs::World& world, engine::ecs::Entity entity) {
    view_model_->title = entity_display_name(world, entity);
    view_model_->subtitle = std::string(kEntity);
    const bool editable = game_ == nullptr;
    show_sections(inspect_entity(world, entity, editable));
    show_add(&world, entity, editable);
}

void InspectorPanel::show_sections(std::vector<InspectorSection> content) {
    content_ = std::move(content);
    sections_.resize(content_.size());
    lines_.resize(content_.size());
    fields_.resize(content_.size());
    refreshing_ = true;
    for (std::size_t i = 0; i < content_.size(); ++i) {
        const InspectorSection& section = content_[i];
        if (!sections_[i]) {
            sections_[i] = std::make_shared<InspectorSectionViewModel>(*this);
        }
        const bool expanded = !collapsed_.contains(section.heading);
        InspectorSectionViewModel& view = *sections_[i];
        view.heading = section.heading;
        view.expanded = expanded;
        view.removeDisplay = std::string(section.removable ? "flex" : "none");
        std::vector<std::shared_ptr<InspectorLineViewModel>>& lines = lines_[i];
        lines.resize(expanded ? section.lines.size() : 0);
        for (std::size_t j = 0; j < lines.size(); ++j) {
            if (!lines[j]) {
                lines[j] = std::make_shared<InspectorLineViewModel>();
            }
            lines[j]->text = section.lines[j];
        }
        view.lines.set(lines);
        std::vector<std::shared_ptr<InspectorFieldViewModel>>& fields = fields_[i];
        fields.resize(expanded ? section.fields.size() : 0);
        for (std::size_t j = 0; j < fields.size(); ++j) {
            if (!fields[j]) {
                fields[j] = std::make_shared<InspectorFieldViewModel>(*this);
            }
            fields[j]->show(section.heading, section.fields[j], editing_key_);
        }
        view.fields.set(fields);
    }
    view_model_->sections.set(sections_);
    refreshing_ = false;
}

void InspectorPanel::show_add(const engine::ecs::World* world, engine::ecs::Entity entity, bool editable) {
    view_model_->addDisplay = std::string(editable ? "flex" : "none");
    if (!editable || !add_open_ || world == nullptr || !world->valid(entity)) {
        addables_.clear();
        view_model_->addables.set(addables_);
        return;
    }
    const engine::ecs::WorldSerializer* ctx = world->try_ctx<engine::ecs::WorldSerializer>();
    engine::ecs::WorldSerializer local;
    const engine::ecs::WorldSerializer& serializer = ctx != nullptr ? *ctx : local;
    std::vector<std::shared_ptr<InspectorAddViewModel>> next;
    std::size_t index = 0;
    for (const auto& [name, entry] : serializer.entries()) {
        engine::ecs::FieldTable table;
        engine::ecs::ComponentWriter writer(table);
        if (!entry.write || entry.write(*world, entity, writer)) {
            continue;
        }
        if (index >= addables_.size()) {
            addables_.push_back(std::make_shared<InspectorAddViewModel>(*this));
        }
        addables_[index]->show(name);
        next.push_back(addables_[index]);
        ++index;
    }
    addables_.resize(next.size());
    view_model_->addables.set(std::move(next));
}

bool InspectorPanel::can_edit() const {
    return game_ == nullptr && edit_world_ != nullptr;
}

const EntitySelection* InspectorPanel::edited_entity() const {
    if (!can_edit()) {
        return nullptr;
    }
    const EntitySelection* entity = std::get_if<EntitySelection>(&selection_->target());
    if (entity == nullptr || !edit_world_->valid(entity->entity)) {
        return nullptr;
    }
    return entity;
}

void InspectorPanel::note_history() {
    if (on_history_) {
        on_history_();
    }
}

void InspectorPanel::toggle_add() {
    if (!can_edit()) {
        return;
    }
    add_open_ = !add_open_;
    refresh();
}

void InspectorPanel::add_component(const std::string& name) {
    const EntitySelection* entity = edited_entity();
    if (entity == nullptr || name.empty()) {
        return;
    }
    engine::ecs::FieldTable existing;
    if (write_component(*edit_world_, entity->entity, name, existing)) {
        return;
    }
    engine::ecs::FieldTable added;
    if (!read_component(*edit_world_, entity->entity, name, added, assets_)) {
        return;
    }
    if (!write_component(*edit_world_, entity->entity, name, added)) {
        return;
    }
    end_field_edit();
    add_open_ = false;
    if (journal_ != nullptr) {
        journal_->push(std::make_unique<EntityEditCommand>(edit_world_, assets_, entity->entity, name,
                EntityEditCommand::Change::Add, engine::ecs::FieldTable{}, std::move(added), std::string{}));
        note_history();
    }
    refresh();
}

void InspectorPanel::remove_component(const std::string& name) {
    const EntitySelection* entity = edited_entity();
    if (entity == nullptr || name.empty()) {
        return;
    }
    engine::ecs::FieldTable before;
    if (!write_component(*edit_world_, entity->entity, name, before)) {
        return;
    }
    if (!erase_component(*edit_world_, entity->entity, name)) {
        return;
    }
    end_field_edit();
    if (journal_ != nullptr) {
        journal_->push(std::make_unique<EntityEditCommand>(edit_world_, assets_, entity->entity, name,
                EntityEditCommand::Change::Remove, std::move(before), engine::ecs::FieldTable{}, std::string{}));
        note_history();
    }
    refresh();
}

void InspectorPanel::edit_text(
        const std::string& component, const std::string& key, int slot, const std::string& text) {
    const EntitySelection* entity = edited_entity();
    if (entity == nullptr) {
        return;
    }
    editing_key_ = component + "\n" + key + "\n" + std::to_string(slot);
    engine::ecs::FieldTable before;
    if (!write_component(*edit_world_, entity->entity, component, before)) {
        return;
    }
    engine::ecs::FieldTable after = before;
    if (!assign_text(after, key, slot, text) || after == before) {
        return;
    }
    if (!read_component(*edit_world_, entity->entity, component, after, assets_)) {
        return;
    }
    if (journal_ != nullptr) {
        const std::string coalesce = std::format("{}:{}:{}:{}:{}", entity->entity.index, entity->entity.generation,
                component, key, slot);
        journal_->push(std::make_unique<EntityEditCommand>(edit_world_, assets_, entity->entity, component,
                EntityEditCommand::Change::Set, std::move(before), std::move(after), coalesce));
        note_history();
    }
}

void InspectorPanel::edit_bool(const std::string& component, const std::string& key, bool value) {
    const EntitySelection* entity = edited_entity();
    if (entity == nullptr) {
        return;
    }
    end_field_edit();
    engine::ecs::FieldTable before;
    if (!write_component(*edit_world_, entity->entity, component, before)) {
        return;
    }
    engine::ecs::FieldTable after = before;
    if (!assign_bool(after, key, value) || after == before) {
        return;
    }
    if (!read_component(*edit_world_, entity->entity, component, after, assets_)) {
        return;
    }
    if (journal_ != nullptr) {
        journal_->push(std::make_unique<EntityEditCommand>(edit_world_, assets_, entity->entity, component,
                EntityEditCommand::Change::Set, std::move(before), std::move(after), std::string{}));
        note_history();
    }
}

}
