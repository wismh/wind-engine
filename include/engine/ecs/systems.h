#pragma once

#include <engine/core/window_desc.h>
#include <engine/ecs/world.h>

#include <functional>

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
    // Additive (§21.6): only consulted for a UiCanvas whose window != kPrimaryWindow. Left unset
    // (nullptr), a secondary-window-targeted canvas is silently skipped rather than crashing —
    // most existing callers (tests, single-window games) never set this and don't need to.
    std::function<render::CommandBuffer*(WindowId)> commands_for_window;
};

void register_engine_systems(ecs::World& world, EngineSystemDeps deps = {});

}
