#pragma once

#include <engine/ecs/world.h>

namespace engine {

struct EngineSystemsRegistered {
    bool value = false;
};

void RegisterEngineSystems(ecs::World& world);

}
