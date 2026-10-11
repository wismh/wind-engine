#pragma once

// docs/tech/modules/Core.md

#if !defined(ENGINE_WITH_WINDOW)
#error "engine::Engine requires ENGINE_WITH_WINDOW"
#endif

#include <engine/core/engine_error.h>
#include <engine/core/engine_host.h>
#include <engine/core/engine_services.h>
#include <engine/core/run_hooks.h>
#include <engine/igame.h>

#include <concepts>
#include <expected>
#include <memory>

namespace engine {

template<typename GameT>
concept EngineGame = std::derived_from<GameT, IGame> && std::constructible_from<GameT, const EngineServices&>;

// Standalone windowed game: one `EngineHost` and one `GameT`. Games normally get this from
// `ENGINE_GAME(GameClass)` in <engine/game_entry.h>.
template<EngineGame GameT>
class Engine {
public:
    Engine() = default;
    ~Engine() {
        dispose();
    }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Builds the host and the game. A failure disposes everything and returns the cause; nothing is shown to the
    // user, `ENGINE_GAME` passes it to `report_startup_error`.
    [[nodiscard]] std::expected<void, EngineError> init();
    int run();
    void dispose();

private:
    // Declared after host_, so the game is destroyed first while the services it references live.
    EngineHost host_;
    std::unique_ptr<GameT> game_;
    bool initialized_ = false;
};

template<EngineGame GameT>
std::expected<void, EngineError> Engine<GameT>::init() {
    if (initialized_) {
        return {};
    }
    if (const auto started = host_.init(); !started) {
        return started;
    }
    game_ = std::make_unique<GameT>(host_.services());
    if (const auto opened = host_.open_primary(game_->primary_window()); !opened) {
        game_.reset();
        return opened;
    }
    if (const auto loaded = host_.load_catalog(host_.assets_root()); !loaded) {
        game_.reset();
        host_.dispose();
        return std::unexpected(EngineError::GameCatalog);
    }
    host_.attach_game(*game_);
    initialized_ = true;
    return {};
}

template<EngineGame GameT>
int Engine<GameT>::run() {
    if (!initialized_) {
        return 1;
    }
    GameT& game = *game_;
    return host_.run(RunHooks{
            .on_start = [&game] { game.on_start(); },
            .on_frame_end = {},
            .on_quit = [&game] { game.on_quit(); },
    });
}

template<EngineGame GameT>
void Engine<GameT>::dispose() {
    host_.dispose();
    initialized_ = false;
}

}
