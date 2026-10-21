#include <engine/ecs/world_asset.h>

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
namespace {

std::optional<FieldValue> toml_node_to_field(const toml::node& node);

std::optional<FieldTable> toml_table_to_field_table(const toml::table& table) {
    FieldTable out;
    for (const auto& [k, v]: table) {
        auto converted = toml_node_to_field(v);
        if (!converted) {
            return std::nullopt;
        }
        out.emplace(std::string(k.str()), std::move(*converted));
    }
    return out;
}

std::optional<FieldValue> toml_node_to_field(const toml::node& node) {
    if (const auto* b = node.as_boolean()) {
        return FieldValue{b->get()};
    }
    if (const auto* i = node.as_integer()) {
        return FieldValue{i->get()};
    }
    if (const auto* f = node.as_floating_point()) {
        return FieldValue{f->get()};
    }
    if (const auto* s = node.as_string()) {
        return FieldValue{s->get()};
    }
    if (const toml::array* arr = node.as_array()) {
        FieldArray items;
        items.reserve(arr->size());
        for (const toml::node& elem: *arr) {
            auto converted = toml_node_to_field(elem);
            if (!converted) {
                return std::nullopt;
            }
            items.push_back(std::move(*converted));
        }
        return FieldValue{std::move(items)};
    }
    if (const toml::table* tab = node.as_table()) {
        auto converted = toml_table_to_field_table(*tab);
        if (!converted) {
            return std::nullopt;
        }
        return FieldValue{std::move(*converted)};
    }
    return std::nullopt;
}

void field_to_toml_table(const FieldTable& src, toml::table& dst);

void insert_field_into_array(const FieldValue& val, toml::array& arr) {
    if (const auto* i = val.as_int()) {
        arr.push_back(*i);
    } else if (const auto* b = val.as_bool()) {
        arr.push_back(*b);
    } else if (const auto* s = val.as_string()) {
        arr.push_back(*s);
    } else if (const auto* sub_arr = val.as_array()) {
        toml::array child;
        for (const FieldValue& elem: *sub_arr) {
            insert_field_into_array(elem, child);
        }
        arr.push_back(std::move(child));
    } else if (const auto* sub_tab = val.as_table()) {
        toml::table child;
        field_to_toml_table(*sub_tab, child);
        child.is_inline(true);
        arr.push_back(std::move(child));
    } else if (const auto num = val.as_number()) {
        arr.push_back(*num);
    }
}

void field_to_toml_table(const FieldTable& src, toml::table& dst) {
    for (const auto& [k, val]: src) {
        if (const auto* i = val.as_int()) {
            dst.insert_or_assign(k, *i);
        } else if (const auto* b = val.as_bool()) {
            dst.insert_or_assign(k, *b);
        } else if (const auto* s = val.as_string()) {
            dst.insert_or_assign(k, *s);
        } else if (const auto* sub_arr = val.as_array()) {
            toml::array child;
            for (const FieldValue& elem: *sub_arr) {
                insert_field_into_array(elem, child);
            }
            dst.insert_or_assign(k, std::move(child));
        } else if (const auto* sub_tab = val.as_table()) {
            toml::table child;
            field_to_toml_table(*sub_tab, child);
            child.is_inline(true);
            dst.insert_or_assign(k, std::move(child));
        } else if (const auto num = val.as_number()) {
            dst.insert_or_assign(k, *num);
        }
    }
}

} // namespace

std::expected<FieldTable, std::string> parse_field_table(std::string_view toml_text) {
    try {
        const toml::table root = toml::parse(toml_text);
        auto converted = toml_table_to_field_table(root);
        if (!converted) {
            return std::unexpected(std::string("unsupported TOML value"));
        }
        return std::move(*converted);
    } catch (const toml::parse_error& err) {
        return std::unexpected(std::string(err.description()));
    }
}

std::expected<WorldAsset, WorldLoadError> parse_world_asset(std::string_view toml_text) {
    toml::table root;
    try {
        root = toml::parse(toml_text);
    } catch (const toml::parse_error& err) {
        return std::unexpected(WorldLoadError{WorldErrorKind::InvalidToml, std::string(err.description())});
    }

    WorldAsset asset;
    if (const toml::node* version_node = root.get("version")) {
        const auto v = version_node->value<std::int64_t>();
        if (!v) {
            return std::unexpected(WorldLoadError{WorldErrorKind::InvalidField, "version must be an integer"});
        }
        if (*v != 1) {
            return std::unexpected(
                WorldLoadError{WorldErrorKind::UnsupportedVersion, std::format("unsupported world version {}", *v)});
        }
        asset.version = static_cast<int>(*v);
    }

    if (const toml::node* cam_node = root.get("active_camera")) {
        const auto cam = cam_node->value<std::string>();
        if (!cam) {
            return std::unexpected(WorldLoadError{WorldErrorKind::InvalidField, "active_camera must be a string"});
        }
        asset.active_camera = *cam;
    }

    for (const auto& [k, _]: root) {
        const std::string_view key = k.str();
        if (key != "version" && key != "active_camera" && key != "entities") {
            return std::unexpected(
                WorldLoadError{WorldErrorKind::InvalidField, std::format("unknown top-level key \"{}\"", key)});
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
        for (const toml::node& elem: *arr) {
            const toml::table* ent_tab = elem.as_table();
            if (ent_tab == nullptr) {
                return std::unexpected(
                    WorldLoadError{WorldErrorKind::InvalidEntity, "each [[entities]] entry must be a table"});
            }

            EntityRecord record;
            if (const toml::node* id_node = ent_tab->get("id")) {
                const auto id_str = id_node->value<std::string>();
                if (!id_str || id_str->empty()) {
                    return std::unexpected(
                        WorldLoadError{WorldErrorKind::InvalidEntity, "entity id must be a non-empty string"});
                }
                record.id = *id_str;
            } else {
                record.id = std::format("entity_{}", index);
            }

            if (!ids.insert(record.id).second) {
                return std::unexpected(WorldLoadError{WorldErrorKind::DuplicateEntityId,
                                                      std::format("duplicate entity id \"{}\"", record.id)});
            }

            if (const toml::node* parent_node = ent_tab->get("parent")) {
                const auto parent_str = parent_node->value<std::string>();
                if (!parent_str) {
                    return std::unexpected(
                        WorldLoadError{WorldErrorKind::InvalidEntity,
                                       std::format("entity \"{}\": parent must be a string", record.id)});
                }
                record.parent = *parent_str;
            }

            for (const auto& [k, v]: *ent_tab) {
                const std::string_view key = k.str();
                if (key == "id" || key == "parent") {
                    continue;
                }
                const toml::table* comp_tab = v.as_table();
                if (comp_tab == nullptr) {
                    return std::unexpected(
                        WorldLoadError{WorldErrorKind::InvalidField,
                                       std::format("entity \"{}\": component \"{}\" must be a table", record.id, key)});
                }
                auto converted = toml_table_to_field_table(*comp_tab);
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
        return std::unexpected(
            WorldLoadError{WorldErrorKind::UnknownActiveCamera,
                           std::format("active_camera \"{}\" does not match any entity id", asset.active_camera)});
    }

    for (const EntityRecord& record: asset.entities) {
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
                return std::unexpected(
                    WorldLoadError{WorldErrorKind::HierarchyCycle,
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
        for (const EntityRecord& record: asset.entities) {
            toml::table ent_tab;
            ent_tab.insert("id", record.id);
            if (!record.parent.empty()) {
                ent_tab.insert("parent", record.parent);
            }
            for (const auto& [comp_name, comp_fields]: record.components) {
                toml::table comp_tab;
                field_to_toml_table(comp_fields, comp_tab);
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
