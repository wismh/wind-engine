#pragma once

#include <engine/core/engine_services.h>
#include <engine/igame.h>

namespace game {

// The game: one world and one empty window. Systems go on world() in on_start.
class Game final : public engine::GameBase {
public:
    explicit Game(const engine::EngineServices& services);

    [[nodiscard]] engine::WindowDesc primary_window() const override;
    void on_start() override;
};

}
