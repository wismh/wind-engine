#include "physics_owner.h"

#include <engine/ecs/physics_3d.h>

namespace engine {

bool claimed_by_physics_3d(const ecs::World& world, ecs::Entity entity) {
    return world.try_get<RigidBody3D>(entity) != nullptr;
}

}
