#include "game.h"

#include <engine/log.h>

namespace game {

Game::Game(const engine::EngineServices& services)
    : engine::GameBase(services.worlds) {}

engine::WindowDesc Game::primary_window() const {
    return engine::WindowDesc{.title = "{{name}}", .size = {1280, 720}};
}

void Game::on_start() {
    engine::log::info("{{name}} started");
}

}
