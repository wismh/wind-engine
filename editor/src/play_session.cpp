#include "play_session.h"

#include <engine/audio/audio_system.h>
#include <engine/core/input_system.h>
#include <engine/core/window_control.h>
#include <engine/core/worlds.h>
#include <engine/ecs/world.h>
#include <engine/igame.h>
#include <engine/log.h>

#include <algorithm>
#include <utility>

namespace editor {
namespace {

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

}

PlaySession::PlaySession(const engine::EngineServices& services, IPlayHost& host, std::filesystem::path live_root,
        engine::WindowDesc idle_window)
    : services_(&services)
    , host_(&host)
    , live_root_(std::move(live_root))
    , idle_window_(std::move(idle_window)) {}

PlaySession::~PlaySession() {
    stop();
}

bool PlaySession::playing() const noexcept {
    return game_ != nullptr;
}

std::expected<std::string, std::string> PlaySession::play(const std::filesystem::path& module) {
    if (playing()) {
        return std::unexpected(std::string("Already playing"));
    }
    auto loaded = engine::load_game_module(module, live_root_);
    if (!loaded) {
        return std::unexpected(engine::describe(loaded.error()));
    }
    std::filesystem::path assets_dir = module.parent_path() / "assets";
    if (auto catalog = host_->load_catalog(assets_dir); !catalog) {
        return std::unexpected(catalog.error());
    }

    kept_worlds_.clear();
    services_->worlds.each_world([this](engine::ecs::World& world) { kept_worlds_.push_back(&world); });
    kept_windows_ = services_->windows.open_windows();
    kept_vsync_ = services_->windows.vsync();
    kept_max_fps_ = services_->windows.max_fps();

    engine::IGame* const game = loaded->create(*services_);
    if (game == nullptr) {
        host_->unload_catalog(assets_dir);
        return std::unexpected(std::string("wind_create_game returned no game"));
    }
    module_.emplace(std::move(*loaded));
    game_ = game;
    assets_dir_ = std::move(assets_dir);
    engine::log::info("Editor: play " + path_text(module) + " from " + path_text(module_->live_path()));

    const engine::WindowDesc desc = game->primary_window();
    apply_window(desc);
    host_->attach(*game);
    game->on_start();
    if (engine::ecs::World* const game_world = services_->worlds.world_for(engine::kPrimaryWindow)) {
        host_->attach_tools(*game_world);
    }
    if (desc.style.transparent) {
        return std::string("The game asks for a transparent window; the editor's game window stays opaque.");
    }
    return std::string{};
}

void PlaySession::stop() {
    if (!playing()) {
        return;
    }
    engine::log::info("Editor: stop " + path_text(module_->source_path()));
    // Nothing built from game code may survive the unload at the end. Each step below only touches what
    // the steps before it have not destroyed yet. The panels go first: they read the game world.
    host_->detach_tools();
    game_->on_quit();
    host_->detach();

    std::vector<engine::ecs::World*> doomed;
    services_->worlds.each_world([this, &doomed](engine::ecs::World& world) {
        if (std::ranges::find(kept_worlds_, &world) == kept_worlds_.end()) {
            doomed.push_back(&world);
        }
    });
    for (engine::ecs::World* world : doomed) {
        services_->worlds.destroy(*world);
    }

    engine::IWindowControl& windows = services_->windows;
    for (const engine::WindowId id : windows.open_windows()) {
        if (std::ranges::find(kept_windows_, id) == kept_windows_.end()) {
            windows.close_window(id);
        }
    }

    services_->input.reset();
    services_->audio.stop_all();
    host_->unload_catalog(assets_dir_);

    module_->destroy(game_);
    game_ = nullptr;
    module_.reset();

    windows.set_title(idle_window_.title);
    windows.set_borderless(idle_window_.style.borderless);
    windows.set_always_on_top(idle_window_.style.always_on_top);
    windows.set_vsync(kept_vsync_);
    windows.set_max_fps(kept_max_fps_);
    kept_worlds_.clear();
    kept_windows_.clear();
    assets_dir_.clear();
}

void PlaySession::apply_window(const engine::WindowDesc& desc) {
    engine::IWindowControl& windows = services_->windows;
    windows.set_title(desc.title);
    windows.resize(desc.size);
    if (desc.position) {
        windows.set_position(*desc.position);
    }
    windows.set_borderless(desc.style.borderless);
    windows.set_always_on_top(desc.style.always_on_top);
}

}
