#include "object_inspection.h"

#include "asset_inspection.h"
#include "entity_inspection.h"

#include <engine/ecs/world_asset.h>
#include <engine/resources/assets_db.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace editor {

bool is_object_asset(const AssetSelection& asset) {
    return !asset.directory && asset.path.extension() == ".object";
}

std::vector<InspectorSection> inspect_object(
        const AssetSelection& asset, const engine::AssetsDb* assets, bool editable) {
    std::vector<InspectorSection> sections = inspect_asset(asset);
    // Remove the raw text Content section produced by inspect_asset.
    std::erase_if(sections, [](const InspectorSection& s) { return s.heading == "Content"; });

    std::ifstream in(asset.path, std::ios::binary);
    if (!in.is_open()) {
        sections.push_back(InspectorSection{.heading = "Content", .lines = {"The file does not open."}});
        return sections;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = engine::ecs::parse_field_table(text);
    if (!parsed) {
        sections.push_back(InspectorSection{.heading = "Content", .lines = {"The .object does not parse as TOML."}});
        return sections;
    }

    const auto type_field = parsed->find("type");
    const std::string* type_name = type_field != parsed->end() ? type_field->second.as_string() : nullptr;
    if (type_name == nullptr || type_name->empty()) {
        sections.push_back(InspectorSection{.heading = "Content", .lines = {"The .object has no \"type\" field."}});
        return sections;
    }

    engine::ecs::FieldTable merged;
    if (assets != nullptr) {
        if (auto defs = assets->default_object_fields(*type_name)) {
            merged = std::move(*defs);
        }
    }
    for (auto& [k, v] : *parsed) {
        if (k != "type") {
            merged.insert_or_assign(k, std::move(v));
        }
    }

    InspectorSection object_section{.heading = *type_name, .removable = false};
    if (merged.empty()) {
        object_section.lines.emplace_back("(no fields)");
    } else {
        object_section.fields.reserve(merged.size());
        for (const auto& [k, v] : merged) {
            std::optional<engine::AssetFilter> filter;
            if (assets != nullptr) {
                filter = assets->object_field_filter(*type_name, k);
            }
            object_section.fields.push_back(make_inspector_field(k, v, editable, assets, filter));
        }
    }

    sections.push_back(std::move(object_section));
    return sections;
}

} // namespace editor
