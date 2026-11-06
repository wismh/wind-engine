#pragma once

// Private. The one place 2D asks whether 3D owns an entity. Not installed.

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

namespace engine {

[[nodiscard]] bool claimed_by_physics_3d(const ecs::World& world, ecs::Entity entity);

}
