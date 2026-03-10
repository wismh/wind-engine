#pragma once

// docs/tech/modules/Core.md

#include <engine/core/application_state.h>
#include <engine/core/time.h>
#include <engine/ecs/world.h>
#include <engine/igame.h>
#include <engine/render/canvas.h>

namespace engine {

class IAudioSystem;
class Worlds;

class Host {
public:
    Host(IGame& game, Worlds& worlds, render::ICanvas& canvas, IAudioSystem* audio = nullptr);
    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    void tick(float real_dt = kFixed);
    void resize(int width, int height);

    [[nodiscard]] ecs::World& world();
    [[nodiscard]] ApplicationState& application_state();
    [[nodiscard]] Time& time();

private:
    void write_window_size(int width, int height, bool send_event);

    IGame* game_ = nullptr;
    Worlds* worlds_ = nullptr;
    render::ICanvas* canvas_ = nullptr;
    IAudioSystem* audio_ = nullptr;
};

}
