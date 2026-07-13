#include "entity_edit.h"

#include "inspector_euler.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/resources/assets_db.h>

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace editor {
namespace {

struct ResolvedIds {
    std::unordered_map<std::uint32_t, std::string> by_index;
    std::unordered_map<std::string, engine::ecs::Entity> by_id;
};

// The same ids `capture_world_asset` writes, so a table produced here reads back through `ComponentReader`.
ResolvedIds resolve_ids(const engine::ecs::World& world) {
    ResolvedIds ids;
    const std::vector<engine::ecs::Entity> entities = world.alive_entities();
    std::unordered_map<std::string, int> used;
    ids.by_index.reserve(entities.size());
    ids.by_id.reserve(entities.size());
    for (const engine::ecs::Entity entity : entities) {
        std::string chosen;
        if (const engine::ecs::EntityName* name = world.try_get<engine::ecs::EntityName>(entity);
                name != nullptr && !name->id.empty()) {
            chosen = name->id;
        } else {
            chosen = std::format("entity_{}", entity.index);
        }
        if (used.contains(chosen)) {
            chosen = std::format("{}_{}", chosen, entity.index);
        }
        used.emplace(chosen, 1);
        ids.by_id.emplace(chosen, entity);
        ids.by_index.emplace(entity.index, std::move(chosen));
    }
    return ids;
}

const engine::ecs::WorldSerializer& serializer_of(const engine::ecs::World& world, engine::ecs::WorldSerializer& local) {
    if (const engine::ecs::WorldSerializer* ctx = world.try_ctx<engine::ecs::WorldSerializer>()) {
        return *ctx;
    }
    return local;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

bool parse_all(std::string_view text, std::int64_t& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, out);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

// strtod, not from_chars: Apple's libc++ has no floating-point from_chars before Xcode 16.3.
bool parse_all(std::string_view text, double& out) {
    if (text.empty() || text.size() > 64) {
        return false;
    }
    const std::string copy(text);
    char* end = nullptr;
    out = std::strtod(copy.c_str(), &end);
    return end == copy.c_str() + copy.size();
}

std::optional<engine::ecs::FieldValue> parse_number(std::string_view text, bool integer) {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    if (integer) {
        std::int64_t value = 0;
        if (!parse_all(text, value)) {
            return std::nullopt;
        }
        return engine::ecs::FieldValue{value};
    }
    double value = 0.0;
    if (!parse_all(text, value)) {
        return std::nullopt;
    }
    return engine::ecs::FieldValue{value};
}

bool is_integer(const engine::ecs::FieldValue& value) {
    return value.as_int() != nullptr;
}

}

bool write_component(const engine::ecs::World& world, engine::ecs::Entity entity, std::string_view name,
        engine::ecs::FieldTable& out) {
    engine::ecs::WorldSerializer local;
    const engine::ecs::WorldSerializer::Entry* entry = serializer_of(world, local).find(name);
    if (entry == nullptr || !entry->write) {
        return false;
    }
    const ResolvedIds ids = resolve_ids(world);
    engine::ecs::ComponentWriter writer(out, [&ids](engine::ecs::Entity target) -> std::string {
        const auto found = ids.by_index.find(target.index);
        return found == ids.by_index.end() ? std::string{} : found->second;
    });
    out.clear();
    return entry->write(world, entity, writer);
}

bool read_component(engine::ecs::World& world, engine::ecs::Entity entity, std::string_view name,
        const engine::ecs::FieldTable& table, engine::AssetsDb* assets) {
    engine::ecs::WorldSerializer local;
    const engine::ecs::WorldSerializer::Entry* entry = serializer_of(world, local).find(name);
    if (entry == nullptr || !entry->read || !world.valid(entity)) {
        return false;
    }
    const ResolvedIds ids = resolve_ids(world);
    engine::ecs::ComponentReader reader(
            table,
            [&ids](std::string_view id) -> std::optional<engine::ecs::Entity> {
                const auto found = ids.by_id.find(std::string(id));
                if (found == ids.by_id.end()) {
                    return std::nullopt;
                }
                return found->second;
            },
            assets);
    if (!entry->read(world, entity, reader)) {
        return false;
    }
    engine::ecs::update_world_transforms(world);
    return true;
}

bool erase_component(engine::ecs::World& world, engine::ecs::Entity entity, std::string_view name) {
    engine::ecs::WorldSerializer local;
    const engine::ecs::WorldSerializer::Entry* entry = serializer_of(world, local).find(name);
    if (entry == nullptr || !entry->remove || !world.valid(entity)) {
        return false;
    }
    if (!entry->remove(world, entity)) {
        return false;
    }
    engine::ecs::update_world_transforms(world);
    return true;
}

bool assign_text(engine::ecs::FieldTable& table, std::string_view key, int slot, std::string_view text) {
    const auto found = table.find(key);
    if (found == table.end()) {
        return false;
    }
    if (slot < 0) {
        if (found->second.as_string() != nullptr) {
            found->second = engine::ecs::FieldValue{std::string(text)};
            return true;
        }
        const std::optional<engine::ecs::FieldValue> parsed = parse_number(text, is_integer(found->second));
        if (!parsed) {
            return false;
        }
        found->second = *parsed;
        return true;
    }
    const engine::ecs::FieldArray* array = found->second.as_array();
    if (array == nullptr || slot >= static_cast<int>(array->size())) {
        return false;
    }
    if (key == "rotation" && array->size() == 4) {
        if (slot > 2) {
            return false;
        }
        double parts[4] = {};
        for (int index = 0; index < 4; ++index) {
            const std::optional<double> number = (*array)[static_cast<std::size_t>(index)].as_number();
            if (!number) {
                return false;
            }
            parts[index] = *number;
        }
        const std::optional<engine::ecs::FieldValue> degrees = parse_number(text, false);
        if (!degrees || !degrees->as_number()) {
            return false;
        }
        glm::vec3 euler = euler_degrees_xyz(quat_xyzw(parts[0], parts[1], parts[2], parts[3]));
        euler[slot] = static_cast<float>(*degrees->as_number());
        const glm::quat next = engine::euler_xyz(glm::radians(euler));
        found->second = engine::ecs::FieldValue{engine::ecs::FieldArray{
                engine::ecs::FieldValue{static_cast<double>(next.x)},
                engine::ecs::FieldValue{static_cast<double>(next.y)},
                engine::ecs::FieldValue{static_cast<double>(next.z)},
                engine::ecs::FieldValue{static_cast<double>(next.w)},
        }};
        return true;
    }
    const bool integer = is_integer((*array)[static_cast<std::size_t>(slot)]);
    const std::optional<engine::ecs::FieldValue> parsed = parse_number(text, integer);
    if (!parsed) {
        return false;
    }
    engine::ecs::FieldArray next = *array;
    next[static_cast<std::size_t>(slot)] = *parsed;
    found->second = engine::ecs::FieldValue{std::move(next)};
    return true;
}

bool assign_bool(engine::ecs::FieldTable& table, std::string_view key, bool value) {
    const auto found = table.find(key);
    if (found == table.end() || found->second.as_bool() == nullptr) {
        return false;
    }
    found->second = engine::ecs::FieldValue{value};
    return true;
}

EntityEditCommand::EntityEditCommand(engine::ecs::World* world, engine::AssetsDb* assets, engine::ecs::Entity entity,
        std::string component, Change change, engine::ecs::FieldTable before, engine::ecs::FieldTable after,
        std::string coalesce)
    : world_(world)
    , assets_(assets)
    , entity_(entity)
    , component_(std::move(component))
    , change_(change)
    , before_(std::move(before))
    , after_(std::move(after))
    , coalesce_(std::move(coalesce)) {}

bool EntityEditCommand::undo() {
    return apply(false);
}

bool EntityEditCommand::redo() {
    return apply(true);
}

bool EntityEditCommand::merges_with(const EditorCommand& other) const {
    if (coalesce_.empty()) {
        return false;
    }
    const auto* newer = dynamic_cast<const EntityEditCommand*>(&other);
    return newer != nullptr && newer->coalesce_ == coalesce_;
}

void EntityEditCommand::merge_from(EditorCommand& newer) {
    after_ = static_cast<EntityEditCommand&>(newer).after_;
}

bool EntityEditCommand::apply(bool forward) {
    if (world_ == nullptr || !world_->valid(entity_)) {
        // The entity is gone. Skipping the step keeps the stack moving.
        return true;
    }
    const bool adding = change_ == Change::Add ? forward : change_ == Change::Remove ? !forward : forward;
    if (change_ == Change::Set) {
        return read_component(*world_, entity_, component_, forward ? after_ : before_, assets_);
    }
    if (adding) {
        return read_component(*world_, entity_, component_, forward ? after_ : before_, assets_);
    }
    return erase_component(*world_, entity_, component_);
}

}
