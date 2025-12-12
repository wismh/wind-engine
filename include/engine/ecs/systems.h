#pragma once

// docs/tech/modules/ECS.md

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
class IWindowControl;
class Worlds;

struct EngineSystemsRegistered {
    bool value = false;
};

struct SimulationSystemsRegistered {
    bool value = false;
};

struct UiSystemsRegistered {
    bool value = false;
};

struct AudioSystemsRegistered {
    bool value = false;
};

struct EngineSystemDeps {
    render::CommandBuffer* commands = nullptr;
    // Where the registered systems report a broken world. Left unset, register_* fills in default_fatal_error()
    // (stderr + abort); Worlds::set_deps fills in its own.
    IFatalError* fatal = nullptr;
    AssetsDb* assets = nullptr;
    IAudioSystem* audio = nullptr;
    // Command buffer for a window, including `kPrimaryWindow`. Left unset, `commands` is the
    // primary buffer and any other window is skipped.
    std::function<render::CommandBuffer*(WindowId)> commands_for_window;
    // Registers an image/font with `window`'s own NanoVG atlas the first time run_ui_render finds
    // it referenced by a drawn UiCanvas's document/stylesheet (ecs/systems.cpp) — idempotent and
    // cheap to call every frame once loaded, so no separate "already loaded" bookkeeping is kept
    // here. Left unset, UI images/fonts beyond the builtin font simply never appear — same
    // graceful-skip shape as commands_for_window.
    std::function<void(WindowId, AssetId)> ensure_ui_image;
    std::function<void(WindowId, AssetId)> ensure_ui_font;
    // Opens and closes the OS windows of DockFloatMode::OsWindow floats (ui/dock_space.h) and binds each to the
    // world of its dock space. Either unset: such floats stay virtual.
    IWindowControl* windows = nullptr;
    Worlds* worlds = nullptr;
};

void register_simulation_systems(ecs::World& world, EngineSystemDeps deps = {});
void register_ui_systems(ecs::World& world, EngineSystemDeps deps = {});
void register_audio_systems(ecs::World& world, EngineSystemDeps deps = {});
void register_engine_systems(ecs::World& world, EngineSystemDeps deps = {});

void run_sprite_animations(ecs::World& world);
// Frame / Input, after the UI input systems: hover, press, and click on `PointerTarget`s, and their share of the
// click-through set. See `PointerTarget` in <engine/ecs/pointer_target.h>.
void run_pointer_targets(ecs::World& world);
void run_particles(ecs::World& world);

}
