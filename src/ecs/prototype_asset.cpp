#include <engine/ecs/prototype_asset.h>

#include "toml_field_helpers.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/resources/assets_db.h>

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

std::expected<PrototypeAsset, PrototypeLoadError> parse_prototype_asset(std::string_view toml_text) {
    toml::table root;
    try {
        root = toml::parse(toml_text);
    } catch (const toml::parse_error& err) {
        return std::unexpected(
                PrototypeLoadError{PrototypeErrorKind::InvalidToml, std::string(err.description())});
    }

    PrototypeAsset asset;
    if (const toml::node* version_node = root.get("version")) {
        const auto v = version_node->value<std::int64_t>();
        if (!v) {
            return std::unexpected(
                    PrototypeLoadError{PrototypeErrorKind::InvalidField, "version must be an integer"});
        }
        if (*v != 1) {
            return std::unexpected(PrototypeLoadError{
                    PrototypeErrorKind::UnsupportedVersion,
                    std::format("unsupported prototype version {}", *v)});
        }
        asset.version = static_cast<int>(*v);
    }

    if (const toml::node* base_node = root.get("base")) {
        const auto base_str = base_node->value<std::string>();
        if (!base_str) {
            return std::unexpected(
                    PrototypeLoadError{PrototypeErrorKind::InvalidField, "base must be a string"});
        }
        const auto parsed_base = AssetId::parse(*base_str);
        if (!parsed_base) {
            return std::unexpected(PrototypeLoadError{
                    PrototypeErrorKind::InvalidField,
                    std::format("invalid base prototype guid \"{}\"", *base_str)});
        }
        asset.base = *parsed_base;
    }

    for (const auto& [k, _] : root) {
        const std::string_view key = k.str();
        if (key != "version" && key != "base" && key != "entities") {
            return std::unexpected(PrototypeLoadError{
                    PrototypeErrorKind::InvalidField, std::format("unknown top-level key \"{}\"", key)});
        }
    }

    std::unordered_map<std::string, std::string> parent_by_id;
    std::unordered_set<std::string> ids;

    if (const toml::node* entities_node = root.get("entities")) {
        const toml::array* arr = entities_node->as_array();
        if (arr == nullptr) {
            return std::unexpected(PrototypeLoadError{
                    PrototypeErrorKind::InvalidEntity, "entities must be an array of tables"});
        }
        std::size_t index = 0;
        for (const toml::node& elem : *arr) {
            const toml::table* ent_tab = elem.as_table();
            if (ent_tab == nullptr) {
                return std::unexpected(PrototypeLoadError{
                        PrototypeErrorKind::InvalidEntity, "each [[entities]] entry must be a table"});
            }

            EntityRecord record;
            if (const toml::node* id_node = ent_tab->get("id")) {
                const auto id_str = id_node->value<std::string>();
                if (!id_str || id_str->empty()) {
                    return std::unexpected(PrototypeLoadError{
                            PrototypeErrorKind::InvalidEntity, "entity id must be a non-empty string"});
                }
                record.id = *id_str;
            } else {
                record.id = std::format("entity_{}", index);
            }

            if (!ids.insert(record.id).second) {
                return std::unexpected(PrototypeLoadError{
                        PrototypeErrorKind::DuplicateEntityId,
                        std::format("duplicate entity id \"{}\"", record.id)});
            }

            if (const toml::node* parent_node = ent_tab->get("parent")) {
                const auto parent_str = parent_node->value<std::string>();
                if (!parent_str) {
                    return std::unexpected(PrototypeLoadError{
                            PrototypeErrorKind::InvalidEntity,
                            std::format("entity \"{}\": parent must be a string", record.id)});
                }
                record.parent = *parent_str;
            }

            if (const toml::node* proto_node = ent_tab->get("prototype")) {
                const auto proto_str = proto_node->value<std::string>();
                if (!proto_str) {
                    return std::unexpected(PrototypeLoadError{
                            PrototypeErrorKind::InvalidField,
                            std::format("entity \"{}\": prototype must be a string", record.id)});
                }
                const auto parsed_id = AssetId::parse(*proto_str);
                if (!parsed_id) {
                    return std::unexpected(PrototypeLoadError{
                            PrototypeErrorKind::InvalidField,
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
                    return std::unexpected(PrototypeLoadError{
                            PrototypeErrorKind::InvalidField,
                            std::format("entity \"{}\": component \"{}\" must be a table", record.id, key)});
                }
                auto converted = detail::toml_table_to_field_table(*comp_tab);
                if (!converted) {
                    return std::unexpected(PrototypeLoadError{
                            PrototypeErrorKind::InvalidField,
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

    // Check hierarchy validity within this asset (if not inheriting from base or if parent is local)
    for (const EntityRecord& record : asset.entities) {
        if (record.parent.empty()) {
            continue;
        }
        // If this prototype has a base, a parent may refer to an entity in base, so unknown parent is allowed
        // only when base is present. When base is nullopt, parent MUST exist in ids.
        if (!asset.base.has_value() && !ids.contains(record.parent)) {
            return std::unexpected(PrototypeLoadError{
                    PrototypeErrorKind::UnknownParent,
                    std::format("entity \"{}\" references unknown parent \"{}\"", record.id, record.parent)});
        }
        std::string cursor = record.parent;
        std::size_t depth = 1;
        while (!cursor.empty()) {
            if (cursor == record.id || depth > kMaxHierarchyDepth) {
                return std::unexpected(PrototypeLoadError{
                        PrototypeErrorKind::HierarchyCycle,
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

std::string serialize_prototype_asset(const PrototypeAsset& asset) {
    toml::table root;
    root.insert("version", static_cast<std::int64_t>(asset.version));
    if (asset.base.has_value()) {
        root.insert("base", asset.base->uri());
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
                ent_tab.insert("prototype", record.prototype->uri());
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

std::expected<PrototypeAsset, PrototypeLoadError> resolve_prototype_asset(
        const PrototypeAsset& asset, AssetsDb* assets) {
    if (!asset.base.has_value()) {
        return asset;
    }

    if (assets == nullptr) {
        return std::unexpected(PrototypeLoadError{
                PrototypeErrorKind::AssetError,
                "cannot resolve prototype variant without AssetsDb"});
    }

    std::unordered_set<std::string> visited;

    const auto resolve_chain = [&](auto self, const PrototypeAsset& current)
            -> std::expected<PrototypeAsset, PrototypeLoadError> {
        if (!current.base.has_value()) {
            return current;
        }

        const std::string base_hex{current.base->hex()};
        if (!visited.insert(base_hex).second) {
            return std::unexpected(PrototypeLoadError{
                    PrototypeErrorKind::PrototypeCycle,
                    std::format("prototype variant cycle detected involving \"{}\"", base_hex)});
        }

        auto base_ptr = assets->try_get<PrototypeAsset>(*current.base);
        if (!base_ptr) {
            return std::unexpected(PrototypeLoadError{
                    PrototypeErrorKind::AssetError,
                    std::format("failed to load base prototype \"{}\"", base_hex)});
        }

        auto resolved_base = self(self, **base_ptr);
        if (!resolved_base) {
            return resolved_base;
        }

        PrototypeAsset merged = std::move(*resolved_base);
        std::unordered_map<std::string, std::size_t> entity_index_by_id;
        for (std::size_t i = 0; i < merged.entities.size(); ++i) {
            entity_index_by_id.emplace(merged.entities[i].id, i);
        }

        for (const EntityRecord& ent : current.entities) {
            const auto it = entity_index_by_id.find(ent.id);
            if (it != entity_index_by_id.end()) {
                EntityRecord& existing = merged.entities[it->second];
                if (!ent.parent.empty()) {
                    existing.parent = ent.parent;
                }
                if (ent.prototype.has_value()) {
                    existing.prototype = ent.prototype;
                }
                for (const auto& [comp_name, comp_table] : ent.components) {
                    auto comp_it = existing.components.find(comp_name);
                    if (comp_it != existing.components.end()) {
                        merge_field_tables(comp_it->second, comp_table);
                    } else {
                        existing.components.insert_or_assign(comp_name, comp_table);
                    }
                }
            } else {
                entity_index_by_id.emplace(ent.id, merged.entities.size());
                merged.entities.push_back(ent);
            }
        }

        merged.base = std::nullopt;
        return merged;
    };

    auto resolved = resolve_chain(resolve_chain, asset);
    if (!resolved) {
        return resolved;
    }

    std::unordered_set<std::string> ids;
    std::unordered_map<std::string, std::string> parent_by_id;
    for (const EntityRecord& record : resolved->entities) {
        ids.insert(record.id);
        if (!record.parent.empty()) {
            parent_by_id.emplace(record.id, record.parent);
        }
    }

    for (const EntityRecord& record : resolved->entities) {
        if (record.parent.empty()) {
            continue;
        }
        if (!ids.contains(record.parent)) {
            return std::unexpected(PrototypeLoadError{
                    PrototypeErrorKind::UnknownParent,
                    std::format("entity \"{}\" references unknown parent \"{}\"", record.id, record.parent)});
        }
        std::string cursor = record.parent;
        std::size_t depth = 1;
        while (!cursor.empty()) {
            if (cursor == record.id || depth > kMaxHierarchyDepth) {
                return std::unexpected(PrototypeLoadError{
                        PrototypeErrorKind::HierarchyCycle,
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

    return resolved;
}

std::expected<Entity, WorldLoadError> spawn_prototype(
        World& world,
        AssetId prototype_id,
        AssetsDb& assets,
        const SpawnPrototypeParams& params,
        const WorldSerializer* serializer) {
    WorldAsset temp_world;
    EntityRecord root_record;
    root_record.id = params.name;
    root_record.prototype = prototype_id;

    if (params.position || params.rotation || params.scale) {
        FieldTable transform_table;
        if (params.position) {
            transform_table["position"] = FieldArray{
                    FieldValue{params.position->x},
                    FieldValue{params.position->y},
                    FieldValue{params.position->z}};
        }
        if (params.rotation) {
            transform_table["rotation"] = FieldArray{
                    FieldValue{params.rotation->x},
                    FieldValue{params.rotation->y},
                    FieldValue{params.rotation->z},
                    FieldValue{params.rotation->w}};
        }
        if (params.scale) {
            transform_table["scale"] = FieldArray{
                    FieldValue{params.scale->x},
                    FieldValue{params.scale->y},
                    FieldValue{params.scale->z}};
        }
        root_record.components.insert_or_assign("transform", std::move(transform_table));
    }

    temp_world.entities.push_back(std::move(root_record));

    auto instantiated = instantiate_world(world, temp_world, &assets, serializer);
    if (!instantiated) {
        return std::unexpected(instantiated.error());
    }

    const Entity root_entity = instantiated->by_id.at(params.name);
    if (params.parent != Entity{} && world.valid(params.parent)) {
        (void)set_parent(world, root_entity, params.parent);
        update_world_transforms(world);
    }

    return root_entity;
}

} // namespace engine::ecs
