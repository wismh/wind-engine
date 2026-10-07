#include <engine/ecs/world_asset.h>

#include "toml_field_helpers.h"

#include <engine/ecs/hierarchy.h>

#include <toml++/toml.hpp>

#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::ecs {

std::expected<FieldTable, std::string> parse_field_table(std::string_view toml_text) {
    try {
        const toml::table root = toml::parse(toml_text);
        auto converted = detail::toml_table_to_field_table(root);
        if (!converted) {
            return std::unexpected(std::string("unsupported TOML value"));
        }
        return std::move(*converted);
    } catch (const toml::parse_error& err) {
        return std::unexpected(std::string(err.description()));
    }
}

std::string serialize_field_table(const FieldTable& table) {
    toml::table root;
    const auto type_field = table.find("type");
    if (type_field != table.end() && type_field->second.as_string() != nullptr) {
        root.insert("type", *type_field->second.as_string());
    }
    detail::field_to_toml_table(table, root);
    std::ostringstream os;
    os << root << '\n';
    return os.str();
}

std::expected<WorldAsset, WorldLoadError> parse_world_asset(std::string_view toml_text) {
    toml::table root;
    try {
        root = toml::parse(toml_text);
    } catch (const toml::parse_error& err) {
        return std::unexpected(
                WorldLoadError{WorldErrorKind::InvalidToml, std::string(err.description())});
    }

    WorldAsset asset;
    if (const toml::node* version_node = root.get("version")) {
        const auto v = version_node->value<std::int64_t>();
        if (!v) {
            return std::unexpected(
                    WorldLoadError{WorldErrorKind::InvalidField, "version must be an integer"});
        }
        if (*v != 1) {
            return std::unexpected(WorldLoadError{
                    WorldErrorKind::UnsupportedVersion, std::format("unsupported world version {}", *v)});
        }
        asset.version = static_cast<int>(*v);
    }

    if (const toml::node* cam_node = root.get("active_camera")) {
        const auto cam = cam_node->value<std::string>();
        if (!cam) {
            return std::unexpected(
                    WorldLoadError{WorldErrorKind::InvalidField, "active_camera must be a string"});
        }
        asset.active_camera = *cam;
    }

    for (const auto& [k, _] : root) {
        const std::string_view key = k.str();
        if (key != "version" && key != "active_camera" && key != "entities") {
            return std::unexpected(WorldLoadError{
                    WorldErrorKind::InvalidField, std::format("unknown top-level key \"{}\"", key)});
        }
    }

    std::unordered_map<std::string, std::string> parent_by_id;
    std::unordered_set<std::string> ids;

    if (const toml::node* entities_node = root.get("entities")) {
        const toml::array* arr = entities_node->as_array();
        if (arr == nullptr) {
            return std::unexpected(
                    WorldLoadError{WorldErrorKind::InvalidEntity, "entities must be an array of tables"});
        }
        std::size_t index = 0;
        for (const toml::node& elem : *arr) {
            const toml::table* ent_tab = elem.as_table();
            if (ent_tab == nullptr) {
                return std::unexpected(
                        WorldLoadError{WorldErrorKind::InvalidEntity, "each [[entities]] entry must be a table"});
            }

            EntityRecord record;
            if (const toml::node* id_node = ent_tab->get("id")) {
                const auto id_str = id_node->value<std::string>();
                if (!id_str || id_str->empty()) {
                    return std::unexpected(WorldLoadError{
                            WorldErrorKind::InvalidEntity, "entity id must be a non-empty string"});
                }
                record.id = *id_str;
            } else {
                record.id = std::format("entity_{}", index);
            }

            if (!ids.insert(record.id).second) {
                return std::unexpected(WorldLoadError{
                        WorldErrorKind::DuplicateEntityId,
                        std::format("duplicate entity id \"{}\"", record.id)});
            }

            if (const toml::node* parent_node = ent_tab->get("parent")) {
                const auto parent_str = parent_node->value<std::string>();
                if (!parent_str) {
                    return std::unexpected(WorldLoadError{
                            WorldErrorKind::InvalidEntity,
                            std::format("entity \"{}\": parent must be a string", record.id)});
                }
                record.parent = *parent_str;
            }

            if (const toml::node* proto_node = ent_tab->get("prototype")) {
                const auto proto_str = proto_node->value<std::string>();
                if (!proto_str) {
                    return std::unexpected(WorldLoadError{
                            WorldErrorKind::InvalidField,
                            std::format("entity \"{}\": prototype must be a string", record.id)});
                }
                const auto parsed_id = AssetId::parse(*proto_str);
                if (!parsed_id) {
                    return std::unexpected(WorldLoadError{
                            WorldErrorKind::InvalidField,
                            std::format("entity \"{}\": invalid prototype guid \"{}\"", record.id, *proto_str)});
                }
                record.prototype = *parsed_id;
            }

            for (const auto& [k, v] : *ent_tab) {
                const std::string_view key = k.str();
                if (key == "id" || key == "parent" || key == "prototype") {
                    continue;
                }
                const toml::table* comp_tab = v.as_table();
                if (comp_tab == nullptr) {
                    return std::unexpected(WorldLoadError{
                            WorldErrorKind::InvalidField,
                            std::format("entity \"{}\": component \"{}\" must be a table", record.id, key)});
                }
                auto converted = detail::toml_table_to_field_table(*comp_tab);
                if (!converted) {
                    return std::unexpected(WorldLoadError{
                            WorldErrorKind::InvalidField,
                            std::format("entity \"{}\": unsupported value in component \"{}\"", record.id, key)});
                }
                record.components.emplace(std::string(key), std::move(*converted));
            }

            if (!record.parent.empty()) {
                parent_by_id.emplace(record.id, record.parent);
            }
            asset.entities.push_back(std::move(record));
            ++index;
        }
    }

    if (!asset.active_camera.empty() && !ids.contains(asset.active_camera)) {
        return std::unexpected(WorldLoadError{
                WorldErrorKind::UnknownActiveCamera,
                std::format("active_camera \"{}\" does not match any entity id", asset.active_camera)});
    }

    for (const EntityRecord& record : asset.entities) {
        if (record.parent.empty()) {
            continue;
        }
        if (!ids.contains(record.parent)) {
            return std::unexpected(WorldLoadError{
                    WorldErrorKind::UnknownParent,
                    std::format("entity \"{}\" references unknown parent \"{}\"", record.id, record.parent)});
        }
        std::string cursor = record.parent;
        std::size_t depth = 1;
        while (!cursor.empty()) {
            if (cursor == record.id || depth > kMaxHierarchyDepth) {
                return std::unexpected(WorldLoadError{
                        WorldErrorKind::HierarchyCycle,
                        std::format("hierarchy cycle or excessive depth at entity \"{}\"", record.id)});
            }
            const auto it = parent_by_id.find(cursor);
            if (it == parent_by_id.end()) {
                break;
            }
            cursor = it->second;
            ++depth;
        }
    }

    return asset;
}

std::string serialize_world_asset(const WorldAsset& asset) {
    toml::table root;
    root.insert("version", static_cast<std::int64_t>(asset.version));
    if (!asset.active_camera.empty()) {
        root.insert("active_camera", asset.active_camera);
    }

    if (!asset.entities.empty()) {
        toml::array entities_arr;
        for (const EntityRecord& record : asset.entities) {
            toml::table ent_tab;
            ent_tab.insert("id", record.id);
            if (!record.parent.empty()) {
                ent_tab.insert("parent", record.parent);
            }
            if (record.prototype.has_value()) {
                ent_tab.insert("prototype", record.prototype->hex());
            }
            for (const auto& [comp_name, comp_fields] : record.components) {
                toml::table comp_tab;
                detail::field_to_toml_table(comp_fields, comp_tab);
                comp_tab.is_inline(true);
                ent_tab.insert(comp_name, std::move(comp_tab));
            }
            entities_arr.push_back(std::move(ent_tab));
        }
        root.insert("entities", std::move(entities_arr));
    }

    std::ostringstream os;
    os << root << '\n';
    return os.str();
}

} // namespace engine::ecs
