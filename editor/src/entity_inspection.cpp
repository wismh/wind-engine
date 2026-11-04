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

std::string asset_display_name(const engine::AssetsDb* assets, std::string_view text) {
    if (text.empty() || text == "00000000000000000000000000000000") {
        return "(None)";
    }
    const auto id = text.starts_with(engine::AssetId::kUriPrefix) ? engine::AssetId::parse(text)
                                                                  : engine::AssetId::parse_hex(text);
    if (id && assets != nullptr) {
        if (const auto* entry = assets->catalog().find(*id)) {
            return std::filesystem::path(entry->relative_path).filename().string();
        }
    }
    return std::string(text);
}

std::string child_path(std::string_view prefix, std::string_view key) {
    if (prefix.empty()) {
        return std::string(key);
    }
    return std::format("{}.{}", prefix, key);
}

std::string section_heading(std::string path, const engine::ecs::FieldTable& table) {
    const auto found = table.find("name");
    if (found == table.end()) {
        return path;
    }
    const std::string* name = found->second.as_string();
    if (name == nullptr || name->empty()) {
        return path;
    }
    path += "  ";
    path += *name;
    return path;
}

enum class ArrayLayout { Empty, ShortNumeric, ScalarElements, Tables, Opaque };

ArrayLayout layout_of(const engine::ecs::FieldArray& array) {
    if (array.empty()) {
        return ArrayLayout::Empty;
    }
    if (numeric_array(array)) {
        return ArrayLayout::ShortNumeric;
    }
    bool numbers = true;
    bool strings = true;
    bool bools = true;
    bool tables = true;
    for (const engine::ecs::FieldValue& item : array) {
        if (!item.as_number()) {
            numbers = false;
        }
        if (item.as_string() == nullptr) {
            strings = false;
        }
        if (item.as_bool() == nullptr) {
            bools = false;
        }
        if (item.as_table() == nullptr) {
            tables = false;
        }
    }
    if (tables) {
        return ArrayLayout::Tables;
    }
    if (numbers || strings || bools) {
        return ArrayLayout::ScalarElements;
    }
    return ArrayLayout::Opaque;
}

std::optional<engine::AssetFilter> direct_filter(
        bool object_filters, const engine::ecs::WorldSerializer* serializer, const engine::AssetsDb* assets,
        std::string_view scope, std::string_view key) {
    if (object_filters) {
        if (assets == nullptr) {
            return std::nullopt;
        }
        return assets->object_field_filter(scope, key);
    }
    if (serializer == nullptr) {
        return std::nullopt;
    }
    return serializer->field_filter(scope, key);
}

InspectorField make_field(
        std::string key, std::string label, const engine::ecs::FieldValue& value, bool editable,
        const engine::AssetsDb* assets, const std::optional<engine::AssetFilter>& filter) {
    InspectorField field;
    field.key = std::move(key);
    field.label = std::move(label);
    const std::string& leaf = field.label;

    if (filter.has_value()) {
        const std::string* s = value.as_string();
        const std::string str = s != nullptr ? *s : "";
        field.text = str;
        field.asset_name = asset_display_name(assets, str);
        if (!editable) {
            field.kind = InspectorFieldKind::Text;
            field.text = std::format("{} = {}", leaf, field.asset_name);
            return field;
        }
        field.kind = InspectorFieldKind::Asset;
        field.asset_filter = filter.value_or(engine::AssetFilter{});
        field.editable = true;
        return field;
    }

    if (const std::string* s = value.as_string(); s != nullptr) {
        const auto id = s->starts_with(engine::AssetId::kUriPrefix) ? engine::AssetId::parse(*s)
                                                                     : engine::AssetId::parse_hex(*s);
        if (id && assets != nullptr) {
            if (const auto* entry = assets->catalog().find(*id)) {
                field.text = *s;
                field.asset_name = std::filesystem::path(entry->relative_path).filename().string();
                if (!editable) {
                    field.kind = InspectorFieldKind::Text;
                    field.text = std::format("{} = {}", leaf, field.asset_name);
                    return field;
                }
                field.kind = InspectorFieldKind::Asset;
                field.asset_filter = engine::AssetFilter{.importer = entry->importer};
                field.editable = true;
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
            if (leaf == "rotation" && array->size() == 4) {
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
            } else if (leaf == "color" && array->size() == 4) {
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
            (leaf == "rotation" || leaf == "color")) {
        std::string shown = "[";
        const int count = 3;
        if (leaf == "rotation") {
            const auto x = (*array)[0].as_number();
            const auto y = (*array)[1].as_number();
            const auto z = (*array)[2].as_number();
            const auto w = (*array)[3].as_number();
            if (x && y && z && w) {
                const glm::vec3 euler = euler_degrees_xyz(quat_xyzw(*x, *y, *z, *w));
                shown += std::format("{}, {}, {}", format_double(euler.x), format_double(euler.y),
                        format_double(euler.z));
                shown += "]";
                field.text = std::format("{} = {}", leaf, shown);
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
            field.text = std::format("{} = {}", leaf, shown);
            return field;
        }
    }
    field.text = std::format("{} = {}", leaf, format_field_value(value));
    return field;
}

void append_table(
        std::vector<InspectorSection>& sections, std::string heading, std::string path_prefix,
        const engine::ecs::FieldTable& table, bool editable, bool removable, bool object_filters,
        std::string_view filter_scope, const engine::ecs::WorldSerializer* serializer,
        const engine::AssetsDb* assets) {
    InspectorSection section{.heading = std::move(heading), .removable = removable};
    if (table.empty()) {
        section.lines.emplace_back("(no fields)");
        sections.push_back(std::move(section));
        return;
    }
    for (const auto& [key, value] : table) {
        const std::string path = child_path(path_prefix, key);
        if (value.as_table() != nullptr) {
            continue;
        }
        if (const engine::ecs::FieldArray* array = value.as_array()) {
            const ArrayLayout layout = layout_of(*array);
            if (layout == ArrayLayout::Empty || layout == ArrayLayout::Tables) {
                continue;
            }
            if (layout == ArrayLayout::ScalarElements) {
                for (std::size_t index = 0; index < array->size(); ++index) {
                    section.fields.push_back(make_field(
                            std::format("{}[{}]", path, index), std::format("{}[{}]", key, index), (*array)[index],
                            editable, assets, std::nullopt));
                }
                continue;
            }
            if (layout == ArrayLayout::Opaque) {
                section.fields.push_back(make_field(path, std::string(key), value, false, assets, std::nullopt));
                continue;
            }
        }
        const std::optional<engine::AssetFilter> filter = path_prefix.empty()
                ? direct_filter(object_filters, serializer, assets, filter_scope, key)
                : std::nullopt;
        section.fields.push_back(make_field(path, std::string(key), value, editable, assets, filter));
    }
    sections.push_back(std::move(section));

    for (const auto& [key, value] : table) {
        const std::string path = child_path(path_prefix, key);
        if (const engine::ecs::FieldTable* nested = value.as_table()) {
            append_table(sections, section_heading(path, *nested), path, *nested, editable, false, object_filters,
                    filter_scope, serializer, assets);
            continue;
        }
        if (const engine::ecs::FieldArray* array = value.as_array()) {
            const ArrayLayout layout = layout_of(*array);
            if (layout == ArrayLayout::Empty) {
                sections.push_back(InspectorSection{.heading = path, .lines = {"(empty)"}});
            } else if (layout == ArrayLayout::Tables) {
                for (std::size_t index = 0; index < array->size(); ++index) {
                    const engine::ecs::FieldTable* element = (*array)[index].as_table();
                    if (element == nullptr) {
                        continue;
                    }
                    const std::string element_path = std::format("{}[{}]", path, index);
                    append_table(sections, section_heading(element_path, *element), element_path, *element, editable,
                            false, object_filters, filter_scope, serializer, assets);
                }
            }
        }
    }
}

}

InspectorField make_inspector_field(
        std::string key, const engine::ecs::FieldValue& value, bool editable,
        const engine::AssetsDb* assets,
        const std::optional<engine::AssetFilter>& filter) {
    return make_field(key, std::move(key), value, editable, assets, filter);
}

void append_inspector_sections(
        std::vector<InspectorSection>& sections, std::string heading, const engine::ecs::FieldTable& table,
        bool editable, bool removable, const engine::AssetsDb* assets,
        const engine::ecs::WorldSerializer* serializer, bool object_filters) {
    const std::string scope = heading;
    append_table(sections, std::move(heading), {}, table, editable, removable, object_filters, scope, serializer,
            assets);
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
            append_inspector_sections(sections, comp_name, table, editable, editable, assets, &serializer, false);
            written.insert(comp_name);
        }
    }

    if (const auto* unknown = world.try_get<engine::ecs::UnknownComponents>(entity)) {
        for (const auto& [comp_name, table] : unknown->components) {
            if (!written.contains(comp_name)) {
                append_inspector_sections(
                        sections, comp_name + " (unregistered)", table, false, false, assets, &serializer, false);
            }
        }
    }

    return sections;
}

}
