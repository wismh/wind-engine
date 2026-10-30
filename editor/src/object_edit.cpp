#include "object_edit.h"

#include "entity_edit.h"

#include <engine/ecs/world_asset.h>
#include <engine/resources/assets_db.h>

#include <format>

#include <fstream>
#include <iterator>
#include <utility>

namespace editor {

bool read_object_table(const std::filesystem::path& path, const engine::AssetsDb* assets,
        std::string& out_type, engine::ecs::FieldTable& out_table) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = engine::ecs::parse_field_table(text);
    if (!parsed) {
        return false;
    }
    const auto type_field = parsed->find("type");
    const std::string* type_name = type_field != parsed->end() ? type_field->second.as_string() : nullptr;
    if (type_name == nullptr || type_name->empty()) {
        return false;
    }
    out_type = *type_name;
    out_table.clear();
    if (assets != nullptr) {
        if (auto defs = assets->default_object_fields(out_type)) {
            out_table = std::move(*defs);
        }
    }
    for (auto& [k, v] : *parsed) {
        if (k != "type") {
            out_table.insert_or_assign(k, std::move(v));
        }
    }
    return true;
}

bool write_object_table(const std::filesystem::path& path, const std::string& type,
        const engine::ecs::FieldTable& table) {
    engine::ecs::FieldTable to_write = table;
    to_write.insert_or_assign("type", engine::ecs::FieldValue{type});
    const std::string toml_text = engine::ecs::serialize_field_table(to_write);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << toml_text;
    return out.good();
}

bool edit_object_field_text(const std::filesystem::path& path, std::string_view project_key,
        const std::string& type, engine::AssetsDb* assets, CommandHistory* journal,
        const std::string& key, int slot, const std::string& text) {
    std::string found_type;
    engine::ecs::FieldTable before;
    if (!read_object_table(path, assets, found_type, before)) {
        return false;
    }
    const std::string use_type = found_type.empty() ? type : found_type;
    engine::ecs::FieldTable after = before;
    if (!assign_text(after, key, slot, text) || after == before) {
        return false;
    }
    if (!write_object_table(path, use_type, after)) {
        return false;
    }
    if (assets != nullptr) {
        assets->clear_object_cache();
    }
    if (journal != nullptr) {
        const std::string coalesce = std::format("{}:{}:{}", project_key, key, slot);
        journal->push(std::make_unique<ObjectEditCommand>(
                path, use_type, assets, std::move(before), std::move(after), coalesce));
    }
    return true;
}

bool edit_object_field_bool(const std::filesystem::path& path,
        const std::string& type, engine::AssetsDb* assets, CommandHistory* journal,
        const std::string& key, bool value) {
    std::string found_type;
    engine::ecs::FieldTable before;
    if (!read_object_table(path, assets, found_type, before)) {
        return false;
    }
    const std::string use_type = found_type.empty() ? type : found_type;
    engine::ecs::FieldTable after = before;
    if (!assign_bool(after, key, value) || after == before) {
        return false;
    }
    if (!write_object_table(path, use_type, after)) {
        return false;
    }
    if (assets != nullptr) {
        assets->clear_object_cache();
    }
    if (journal != nullptr) {
        journal->push(std::make_unique<ObjectEditCommand>(
                path, use_type, assets, std::move(before), std::move(after), std::string{}));
    }
    return true;
}

ObjectEditCommand::ObjectEditCommand(std::filesystem::path path, std::string type, engine::AssetsDb* assets,
        engine::ecs::FieldTable before, engine::ecs::FieldTable after, std::string coalesce)
    : path_(std::move(path)),
      type_(std::move(type)),
      assets_(assets),
      before_(std::move(before)),
      after_(std::move(after)),
      coalesce_(std::move(coalesce)) {}

bool ObjectEditCommand::apply(const engine::ecs::FieldTable& table) {
    if (!write_object_table(path_, type_, table)) {
        return false;
    }
    if (assets_ != nullptr) {
        assets_->clear_object_cache();
    }
    return true;
}

bool ObjectEditCommand::undo() {
    return apply(before_);
}

bool ObjectEditCommand::redo() {
    return apply(after_);
}

bool ObjectEditCommand::valid() const {
    return std::filesystem::exists(path_);
}

bool ObjectEditCommand::merges_with(const EditorCommand& other) const {
    if (coalesce_.empty()) {
        return false;
    }
    const auto* edit = dynamic_cast<const ObjectEditCommand*>(&other);
    return edit != nullptr && edit->coalesce_ == coalesce_;
}

void ObjectEditCommand::merge_from(EditorCommand& newer) {
    auto& edit = static_cast<ObjectEditCommand&>(newer);
    after_ = std::move(edit.after_);
}

} // namespace editor
