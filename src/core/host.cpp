#include <engine/core/host.h>

#include "core/frame_step.h"

#include <engine/core/worlds.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ui/canvas.h>

#include <glm/vec2.hpp>

namespace engine {

Host::Host(IGame& game, Worlds& worlds, render::ICanvas& canvas, IAudioSystem* audio)
    : game_(&game)
    , worlds_(&worlds)
    , canvas_(&canvas)
    , audio_(audio) {
    worlds_->set_deps({});
    register_engine_systems(game_->world());
    worlds_->bind_window(kPrimaryWindow, game_->world());
    worlds_->enable_ui(game_->world());
    worlds_->enable_audio(game_->world());
    const glm::ivec2 size = game_->primary_window().size;
    write_window_size(size.x, size.y, true);
    game_->on_start();
    ui::apply_canvas_fit(game_->world());
}

Host::~Host() {
    game_->on_quit();
}

void Host::tick(float real_dt) {
    flush_worlds(*worlds_);
    simulate_worlds(*worlds_, audio_, real_dt);
    canvas_->draw();
}

void Host::resize(int width, int height) {
    write_window_size(width, height, true);
}

ecs::World& Host::world() {
    return game_->world();
}

ApplicationState& Host::application_state() {
    return worlds_->application_state();
}

Time& Host::time() {
    return game_->world().ctx<Time>();
}

void Host::write_window_size(int width, int height, bool send_event) {
    worlds_->presentation().sizes.sizes[kPrimaryWindow] = ui::WindowSize{width, height};
    ecs::World& world_ref = world();
    if (send_event) {
        ecs::EventWriter<ui::WindowResizeEvent>{world_ref}.send(
                ui::WindowResizeEvent{.window = kPrimaryWindow, .width = width, .height = height});
    }
    ui::apply_canvas_fit(world_ref);
}

}
