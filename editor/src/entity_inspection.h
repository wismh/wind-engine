#pragma once

#include "inspector_section.h"

#include <engine/ecs/entity.h>
#include <engine/ecs/world_io.h>

#include <string>
#include <vector>

namespace engine::ecs {
class World;
class WorldSerializer;
}

namespace engine {
class AssetsDb;
struct AssetFilter;
}

namespace editor {

// Expands `table` into `sections`, starting with `heading`. Nested tables and arrays of tables follow that
// section. `object_filters` reads `AssetsDb::object_field_filter` for direct fields; otherwise filters come from
// `serializer` when it is set. `removable` is the root section only.
void append_inspector_sections(
        std::vector<InspectorSection>& sections, std::string heading, const engine::ecs::FieldTable& table,
        bool editable, bool removable, const engine::AssetsDb* assets, const engine::ecs::WorldSerializer* serializer,
        bool object_filters);

[[nodiscard]] InspectorField make_inspector_field(
        std::string key, const engine::ecs::FieldValue& value, bool editable,
        const engine::AssetsDb* assets = nullptr,
        const std::optional<engine::AssetFilter>& filter = std::nullopt);

// Display name of `entity` in `world`: `EntityName::id` when non-empty, otherwise `entity_<index>`.
[[nodiscard]] std::string entity_display_name(const engine::ecs::World& world, engine::ecs::Entity entity);

// What the Inspector shows for an entity in `world`:
// - Entity: id, handle (<index>:<generation>), parent (when valid), children count (when non-empty), and
//   active_camera (when this entity is the world's ActiveCamera).
// - One section per serialized component on the entity via WorldSerializer.
// - One "<name> (unregistered)" section per component in UnknownComponents not already written by WorldSerializer.
// `editable` turns each registered component into fields (and a remove button on the component heading). Play Mode
// passes false: the same sections, each field a read-only line. A nested table and each element of an array of
// tables become their own section. The field key is the path (`heroes[0].base.max_hp`); the label is the leaf.
[[nodiscard]] std::vector<InspectorSection> inspect_entity(
        const engine::ecs::World& world, engine::ecs::Entity entity, bool editable,
        const engine::AssetsDb* assets = nullptr);

}
