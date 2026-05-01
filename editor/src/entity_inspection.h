#pragma once

#include "inspector_section.h"

#include <engine/ecs/entity.h>

#include <string>
#include <vector>

namespace engine::ecs {
class World;
}

namespace editor {

// Display name of `entity` in `world`: `EntityName::id` when non-empty, otherwise `entity_<index>`.
[[nodiscard]] std::string entity_display_name(const engine::ecs::World& world, engine::ecs::Entity entity);

// What the Inspector shows for an entity in `world`:
// - Entity: id, handle (<index>:<generation>), parent (when valid), children count (when non-empty), and
//   active_camera (when this entity is the world's ActiveCamera).
// - One section per serialized component on the entity via WorldSerializer.
// - One "<name> (unregistered)" section per component in UnknownComponents not already written by WorldSerializer.
[[nodiscard]] std::vector<InspectorSection> inspect_entity(
        const engine::ecs::World& world, engine::ecs::Entity entity);

}
