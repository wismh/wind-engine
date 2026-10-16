#include "entity_inspection.h"

#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>
#include <engine/ecs/world_serializer.h>

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

InspectorSection make_component_section(std::string heading, const engine::ecs::FieldTable& table) {
    InspectorSection section{.heading = std::move(heading)};
    if (table.empty()) {
        section.lines.emplace_back("(no fields)");
        return section;
    }
    section.lines.reserve(table.size());
    for (const auto& [key, val] : table) {
        section.lines.push_back(std::format("{} = {}", key, format_field_value(val)));
    }
    return section;
}

}

std::string entity_display_name(const engine::ecs::World& world, engine::ecs::Entity entity) {
    if (const auto* name = world.try_get<engine::ecs::EntityName>(entity); name != nullptr && !name->id.empty()) {
        return name->id;
    }
    return std::format("entity_{}", entity.index);
}

std::vector<InspectorSection> inspect_entity(const engine::ecs::World& world, engine::ecs::Entity entity) {
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

    std::unordered_set<std::string> written;
    for (const auto& [comp_name, entry] : serializer.entries()) {
        engine::ecs::FieldTable table;
        engine::ecs::ComponentWriter writer(table, entity_to_id);
        if (entry.write(world, entity, writer)) {
            sections.push_back(make_component_section(comp_name, table));
            written.insert(comp_name);
        }
    }

    if (const auto* unknown = world.try_get<engine::ecs::UnknownComponents>(entity)) {
        for (const auto& [comp_name, table] : unknown->components) {
            if (!written.contains(comp_name)) {
                sections.push_back(make_component_section(comp_name + " (unregistered)", table));
            }
        }
    }

    return sections;
}

}
