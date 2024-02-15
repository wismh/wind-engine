#pragma once

#include <engine/ecs/world.h>

namespace engine {

namespace render {
class CommandBuffer;
}

class IFatalError;
class AssetsDb;
class IAudioSystem;

struct EngineSystemsRegistered {
    bool value = false;
};

struct EngineSystemDeps {
    render::CommandBuffer* commands = nullptr;
    IFatalError* fatal = nullptr;
    AssetsDb* assets = nullptr;
    IAudioSystem* audio = nullptr;
};

void register_engine_systems(ecs::World& world, EngineSystemDeps deps = {});

}
