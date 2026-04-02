#pragma once

#include "play_host.h"

#include <engine/core/engine_services.h>
#include <engine/core/game_module.h>
#include <engine/core/window_desc.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace engine {
class IGame;
namespace ecs {
class World;
}
}

namespace editor {

// One game module loaded into the editor between Play and Stop. Everything the game built goes away in
// `stop`, before the module is unloaded: see the order there.
class PlaySession {
public:
    // `live_root` receives the module copies. `idle_window` is what kPrimaryWindow shows between plays:
    // its title and style come back on Stop.
    PlaySession(const engine::EngineServices& services, IPlayHost& host, std::filesystem::path live_root,
            engine::WindowDesc idle_window);
    ~PlaySession();

    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;

    // Loads `module`, its catalog from `assets/` beside it, creates the game, applies its window, attaches
    // it to kPrimaryWindow, calls `on_start`, and attaches the panels to the world of kPrimaryWindow. The value is a warning for the status line (empty when
    // there is none). An error leaves the editor as it was and says why.
    [[nodiscard]] std::expected<std::string, std::string> play(const std::filesystem::path& module);

    // No-op when not playing.
    void stop();

    [[nodiscard]] bool playing() const noexcept;

private:
    void apply_window(const engine::WindowDesc& desc);

    const engine::EngineServices* services_;
    IPlayHost* host_;
    std::filesystem::path live_root_;
    engine::WindowDesc idle_window_;

    std::optional<engine::GameModule> module_;
    engine::IGame* game_ = nullptr;
    std::filesystem::path assets_dir_;
    // What existed before Play survives Stop: the editor's world and window, and kPrimaryWindow.
    std::vector<engine::ecs::World*> kept_worlds_;
    std::vector<engine::WindowId> kept_windows_;
};

}
