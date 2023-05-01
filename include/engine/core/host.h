#pragma once

#include <engine/core/application_state.h>
#include <engine/core/fixed_step.h>
#include <engine/core/time.h>
#include <engine/ecs/world.h>
#include <engine/igame.h>
#include <engine/render/canvas.h>

namespace engine {

class Host {
public:
    Host(IGame& game, render::ICanvas& canvas);
    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    void tick(float real_dt = FIXED);
    void resize(int width, int height);

    [[nodiscard]] ecs::World& world();
    [[nodiscard]] ApplicationState& application_state();
    [[nodiscard]] Time& time();

private:
    void write_window_size(int width, int height, bool send_event);

    IGame* game_ = nullptr;
    render::ICanvas* canvas_ = nullptr;
    Time* time_ = nullptr;
    ApplicationState* app_state_ = nullptr;
    FixedStepClock clock_;
};

}
