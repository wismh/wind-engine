#pragma once

#include <engine/core/window_desc.h>
#include <engine/ecs/world.h>
#include <engine/resources/asset_id.h>

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
    // Additive: only consulted for a UiCanvas whose window != kPrimaryWindow. Left unset
    // (nullptr), a secondary-window-targeted canvas is silently skipped rather than crashing —
    // most existing callers (tests, single-window games) never set this and don't need to.
    std::function<render::CommandBuffer*(WindowId)> commands_for_window;
    // Registers an image/font with `window`'s own NanoVG atlas the first time run_ui_render finds
    // it referenced by a drawn UiCanvas's document/stylesheet (ecs/systems.cpp) — idempotent and
    // cheap to call every frame once loaded, so no separate "already loaded" bookkeeping is kept
    // here. Left unset, UI images/fonts beyond the builtin font simply never appear — same
    // graceful-skip shape as commands_for_window.
    std::function<void(WindowId, AssetId)> ensure_ui_image;
    std::function<void(WindowId, AssetId)> ensure_ui_font;
};

void register_engine_systems(ecs::World& world, EngineSystemDeps deps = {});

void run_sprite_animations(ecs::World& world);
void run_particles(ecs::World& world);

}
