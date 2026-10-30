#include "entity_inspection.h"

#include "inspector_euler.h"

#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>
#include <engine/ecs/world_serializer.h>
#include <engine/resources/asset_filter.h>
#include <engine/resources/assets_db.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace editor {
namespace {

std::string format_double(double value) {
    if (value == 0.0) {
        return "0";
    }
    const float as_float = static_cast<float>(value);
    if (static_cast<double>(as_float) == value) {
        return std::format("{}", as_float);
    }
    return std::format("{}", value);
}

std::string format_field_value(const engine::ecs::FieldValue& value) {
    if (const bool* b = value.as_bool()) {
        return *b ? "true" : "false";
    }
    if (const std::int64_t* i = value.as_int()) {
        return std::format("{}", *i);
    }
    if (const std::string* s = value.as_string()) {
        return s->empty() ? "(empty)" : *s;
    }
    if (const engine::ecs::FieldArray* arr = value.as_array()) {
        std::string out = "[";
        for (std::size_t idx = 0; idx < arr->size(); ++idx) {
            if (idx > 0) {
                out += ", ";
            }
            out += format_field_value((*arr)[idx]);
        }
        out += "]";
        return out;
    }
    if (const engine::ecs::FieldTable* tab = value.as_table()) {
        std::string out = "{";
        bool first = true;
        for (const auto& [k, v] : *tab) {
            if (!first) {
                out += ", ";
            }
            first = false;
            out += k;
            out += " = ";
            out += format_field_value(v);
        }
        out += "}";
        return out;
    }
    if (const auto num = value.as_number()) {
        return format_double(*num);
    }
    return {};
}

bool numeric_array(const engine::ecs::FieldArray& array) {
    if (array.empty() || array.size() > 4) {
        return false;
    }
    for (const engine::ecs::FieldValue& item : array) {
        if (!item.as_number()) {
            return false;
        }
    }
    return true;
}

InspectorField make_field(
        std::string key, const engine::ecs::FieldValue& value, bool editable,
        const engine::AssetsDb* assets, const std::optional<engine::AssetFilter>& filter) {
    InspectorField field;
    field.key = std::move(key);

    if (filter.has_value()) {
        field.kind = InspectorFieldKind::Asset;
        field.asset_filter = filter.value_or(engine::AssetFilter{});
        field.editable = editable;
        const std::string* s = value.as_string();
        const std::string str = s != nullptr ? *s : "";
        field.text = str;
        if (str.empty() || str == "00000000000000000000000000000000") {
            field.asset_name = "(None)";
        } else {
            auto id = engine::AssetId::parse(str);
            if (id && assets != nullptr) {
                if (const auto* entry = assets->catalog().find(*id)) {
                    field.asset_name = std::filesystem::path(entry->relative_path).filename().string();
                } else {
                    field.asset_name = str;
                }
            } else {
                field.asset_name = str;
            }
        }
        return field;
    }

    if (const std::string* s = value.as_string(); s != nullptr && s->size() == 32) {
        if (auto id = engine::AssetId::parse(*s); id && assets != nullptr) {
            if (const auto* entry = assets->catalog().find(*id)) {
                field.kind = InspectorFieldKind::Asset;
                field.asset_filter = engine::AssetFilter{.importer = entry->importer};
                field.editable = editable;
                field.text = *s;
                field.asset_name = std::filesystem::path(entry->relative_path).filename().string();
                return field;
            }
        }
    }

    if (editable) {
        if (const bool* flag = value.as_bool()) {
            field.kind = InspectorFieldKind::Bool;
            field.flag = *flag;
            field.editable = true;
            return field;
        }
        if (const std::string* text = value.as_string()) {
            field.kind = InspectorFieldKind::String;
            field.text = *text;
            field.editable = true;
            return field;
        }
        if (value.as_int() != nullptr) {
            field.kind = InspectorFieldKind::Number;
            field.text = format_field_value(value);
            field.integer = true;
            field.editable = true;
            return field;
        }
        if (const engine::ecs::FieldArray* array = value.as_array(); array != nullptr && numeric_array(*array)) {
            field.kind = InspectorFieldKind::Vec;
            field.axis_count = static_cast<int>(array->size());
            for (int index = 0; index < field.axis_count; ++index) {
                const engine::ecs::FieldValue& item = (*array)[static_cast<std::size_t>(index)];
                field.axes[static_cast<std::size_t>(index)] = format_field_value(item);
                field.axis_integer[static_cast<std::size_t>(index)] = item.as_int() != nullptr;
            }
            // A quaternion stays x,y,z,w in the table. The row edits Euler degrees. A 4-wide color
            // edits rgb and leaves the alpha component in the table.
            if (field.key == "rotation" && array->size() == 4) {
                const auto x = (*array)[0].as_number();
                const auto y = (*array)[1].as_number();
                const auto z = (*array)[2].as_number();
                const auto w = (*array)[3].as_number();
                if (x && y && z && w) {
                    const glm::vec3 euler = euler_degrees_xyz(quat_xyzw(*x, *y, *z, *w));
                    field.axis_count = 3;
                    for (int index = 0; index < 3; ++index) {
                        field.axes[static_cast<std::size_t>(index)] = format_double(euler[index]);
                        field.axis_integer[static_cast<std::size_t>(index)] = false;
                    }
                }
            } else if (field.key == "color" && array->size() == 4) {
                field.axis_count = 3;
            }
            field.editable = true;
            return field;
        }
        if (value.as_number()) {
            field.kind = InspectorFieldKind::Number;
            field.text = format_field_value(value);
            field.editable = true;
            return field;
        }
    }
    field.kind = InspectorFieldKind::Text;
    if (const engine::ecs::FieldArray* array = value.as_array();
            array != nullptr && array->size() == 4 && numeric_array(*array) &&
            (field.key == "rotation" || field.key == "color")) {
        std::string shown = "[";
        const int count = 3;
        if (field.key == "rotation") {
            const auto x = (*array)[0].as_number();
            const auto y = (*array)[1].as_number();
            const auto z = (*array)[2].as_number();
            const auto w = (*array)[3].as_number();
            if (x && y && z && w) {
                const glm::vec3 euler = euler_degrees_xyz(quat_xyzw(*x, *y, *z, *w));
                shown += std::format("{}, {}, {}", format_double(euler.x), format_double(euler.y),
                        format_double(euler.z));
                shown += "]";
                field.text = std::format("{} = {}", field.key, shown);
                return field;
            }
        } else {
            for (int index = 0; index < count; ++index) {
                if (index > 0) {
                    shown += ", ";
                }
                shown += format_field_value((*array)[static_cast<std::size_t>(index)]);
            }
            shown += "]";
            field.text = std::format("{} = {}", field.key, shown);
            return field;
        }
    }
    field.text = std::format("{} = {}", field.key, format_field_value(value));
    return field;
}

InspectorSection make_component_section(
        std::string heading, const engine::ecs::FieldTable& table, bool editable,
        const engine::ecs::WorldSerializer* serializer, const engine::AssetsDb* assets) {
    InspectorSection section{.heading = std::move(heading), .removable = editable};
    if (table.empty()) {
        section.lines.emplace_back("(no fields)");
        return section;
    }
    if (!editable) {
        section.lines.reserve(table.size());
        for (const auto& [key, val] : table) {
            std::optional<engine::AssetFilter> filter;
            if (serializer != nullptr) {
                filter = serializer->field_filter(section.heading, key);
            }
            if (filter.has_value()) {
                const std::string* s = val.as_string();
                const std::string str = s != nullptr ? *s : "";
                std::string display = "(None)";
                if (!str.empty() && str != "00000000000000000000000000000000") {
                    auto id = engine::AssetId::parse(str);
                    if (id && assets != nullptr) {
                        if (const auto* entry = assets->catalog().find(*id)) {
                            display = std::filesystem::path(entry->relative_path).filename().string();
                        } else {
                            display = str;
                        }
                    } else {
                        display = str;
                    }
                }
                section.lines.push_back(std::format("{} = {}", key, display));
            } else {
                section.lines.push_back(std::format("{} = {}", key, format_field_value(val)));
            }
        }
        return section;
    }
    section.fields.reserve(table.size());
    for (const auto& [key, val] : table) {
        std::optional<engine::AssetFilter> filter;
        if (serializer != nullptr) {
            filter = serializer->field_filter(section.heading, key);
        }
        section.fields.push_back(make_field(key, val, true, assets, filter));
    }
    return section;
}

}

InspectorField make_inspector_field(
        std::string key, const engine::ecs::FieldValue& value, bool editable,
        const engine::AssetsDb* assets,
        const std::optional<engine::AssetFilter>& filter) {
    return make_field(std::move(key), value, editable, assets, filter);
}

std::string entity_display_name(const engine::ecs::World& world, engine::ecs::Entity entity) {
    if (const auto* name = world.try_get<engine::ecs::EntityName>(entity); name != nullptr && !name->id.empty()) {
        return name->id;
    }
    return std::format("entity_{}", entity.index);
}

std::vector<InspectorSection> inspect_entity(
        const engine::ecs::World& world, engine::ecs::Entity entity, bool editable,
        const engine::AssetsDb* assets) {
    if (!world.valid(entity)) {
        return {};
    }

    std::vector<InspectorSection> sections;

    InspectorSection entity_section{.heading = "Entity"};
    const auto* name = world.try_get<engine::ecs::EntityName>(entity);
    const std::string_view id_text =
            (name != nullptr && !name->id.empty()) ? std::string_view(name->id) : std::string_view("(unnamed)");
    entity_section.lines.push_back(std::format("id: {}", id_text));
    entity_section.lines.push_back(std::format("handle: {}:{}", entity.index, entity.generation));

    if (const auto* parent = world.try_get<engine::Parent>(entity);
            parent != nullptr && world.valid(parent->entity)) {
        entity_section.lines.push_back(std::format("parent: {}", entity_display_name(world, parent->entity)));
    }

    if (const auto* children = world.try_get<engine::Children>(entity)) {
        std::size_t child_count = 0;
        for (const engine::ecs::Entity child : children->entities) {
            if (world.valid(child)) {
                ++child_count;
            }
        }
        if (child_count > 0) {
            entity_section.lines.push_back(std::format("children: {}", child_count));
        }
    }

    if (const auto* active_cam = world.try_ctx<engine::ecs::ActiveCamera>();
            active_cam != nullptr && active_cam->entity == entity) {
        entity_section.lines.emplace_back("active_camera: true");
    }

    sections.push_back(std::move(entity_section));

    const engine::ecs::WorldSerializer default_serializer;
    const engine::ecs::WorldSerializer* ctx_serializer = world.try_ctx<engine::ecs::WorldSerializer>();
    const engine::ecs::WorldSerializer& serializer =
            ctx_serializer != nullptr ? *ctx_serializer : default_serializer;

    const auto entity_to_id = [&world](engine::ecs::Entity target) -> std::string {
        if (!world.valid(target)) {
            return {};
        }
        return entity_display_name(world, target);
    };

    if (assets == nullptr) {
        assets = world.try_ctx<engine::AssetsDb>();
    }

    std::unordered_set<std::string> written;
    for (const auto& [comp_name, entry] : serializer.entries()) {
        engine::ecs::FieldTable table;
        engine::ecs::ComponentWriter writer(table, entity_to_id);
        if (entry.write(world, entity, writer)) {
            sections.push_back(make_component_section(comp_name, table, editable, &serializer, assets));
            written.insert(comp_name);
        }
    }

    if (const auto* unknown = world.try_get<engine::ecs::UnknownComponents>(entity)) {
        for (const auto& [comp_name, table] : unknown->components) {
            if (!written.contains(comp_name)) {
                sections.push_back(make_component_section(comp_name + " (unregistered)", table, false, &serializer, assets));
            }
        }
    }

    return sections;
}

}
