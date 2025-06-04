#pragma once

// docs/tech/modules/Core.md

#if !defined(ENGINE_WITH_WINDOW)
#error "engine::Engine requires ENGINE_WITH_WINDOW"
#endif

#include <engine/core/engine_host.h>
#include <engine/core/engine_services.h>
#include <engine/core/run_hooks.h>
#include <engine/igame.h>
#include <engine/resources/fatal_error.h>

#include <concepts>
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

    bool init();
    int run();
    void dispose();

private:
    // Declared after host_, so the game is destroyed first while the services it references live.
    EngineHost host_;
    std::unique_ptr<GameT> game_;
    bool initialized_ = false;
};

template<EngineGame GameT>
bool Engine<GameT>::init() {
    if (initialized_) {
        return true;
    }
    if (!host_.init()) {
        return false;
    }
    game_ = std::make_unique<GameT>(host_.services());
    if (!host_.open_primary(game_->primary_window())) {
        return false;
    }
    if (!host_.load_catalog(host_.assets_root())) {
        host_.fatal().report("Failed to load game catalog");
        host_.dispose();
        return false;
    }
    host_.attach_game(*game_);
    initialized_ = true;
    return true;
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
