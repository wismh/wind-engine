#include <engine/core/host.h>

#include "core/frame_step.h"

#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ui/canvas.h>

#include <glm/vec2.hpp>

namespace engine {

Host::Host(IGame& game, render::ICanvas& canvas, IAudioSystem* audio)
    : game_(&game)
    , canvas_(&canvas)
    , audio_(audio)
    , time_(&game.world().ctx<Time>())
    , app_state_(&game.world().ctx<ApplicationState>())
    , clock_(*time_, *app_state_) {
    const glm::ivec2 size = game_->primary_window().size;
    write_window_size(size.x, size.y, true);
    register_engine_systems(game_->world());
    game_->on_start();
    ui::apply_canvas_fit(game_->world());
}

Host::~Host() {
    game_->on_quit();
}

void Host::tick(float real_dt) {
    flush_game_events(*game_);
    simulate_game_frame(*game_, audio_, clock_, real_dt);
    canvas_->draw();
}

void Host::resize(int width, int height) {
    write_window_size(width, height, true);
}

ecs::World& Host::world() {
    return game_->world();
}

ApplicationState& Host::application_state() {
    return *app_state_;
}

Time& Host::time() {
    return *time_;
}

void Host::write_window_size(int width, int height, bool send_event) {
    ecs::World& world_ref = world();
    world_ref.ctx<ui::WindowSizes>().sizes[kPrimaryWindow] = ui::WindowSize{width, height};
    if (send_event) {
        ecs::EventWriter<ui::WindowResizeEvent>{world_ref}.send(
                ui::WindowResizeEvent{.window = kPrimaryWindow, .width = width, .height = height});
    }
    ui::apply_canvas_fit(world_ref);
}

}
