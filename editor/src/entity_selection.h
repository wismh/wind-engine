#pragma once

#include <engine/ecs/entity.h>

namespace editor {

// An entity selected in the Hierarchy tab (from the open `.world` preview in Edit Mode or the live game world
// during Play).
struct EntitySelection {
    engine::ecs::Entity entity{};

    bool operator==(const EntitySelection&) const = default;
};

}
