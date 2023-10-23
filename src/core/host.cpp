#include <engine/core/host.h>

#include <engine/audio/audio_system.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ui/canvas.h>

#include <glm/vec2.hpp>

namespace engine {

Host::Host(IGame& game, render::ICanvas& canvas, IAudioSystem* audio)
    : game_(&game)
    , canvas_(&canvas)
    , audio_(audio)
    , time_(&game.World().ctx<Time>())
    , app_state_(&game.World().ctx<ApplicationState>())
    , clock_(*time_, *app_state_) {
    const glm::ivec2 size = game_->WindowSize();
    write_window_size(size.x, size.y, true);
    RegisterEngineSystems(game_->World());
    game_->OnStart();
    ui::apply_fill_window(game_->World());
}

Host::~Host() {
    game_->OnQuit();
}

void Host::tick(float real_dt) {
    ecs::World& world_ref = world();
    world_ref.FlushEvents();
    ui::begin_frame(world_ref);

    const int steps = clock_.advance(real_dt);
    if (audio_ != nullptr) {
        audio_->Update(time_->deltaTime);
    }
    for (int i = 0; i < steps; ++i) {
        game_->OnFixedUpdate();
    }
    game_->OnUpdate();
    canvas_->Draw();
}

void Host::resize(int width, int height) {
    write_window_size(width, height, true);
}

ecs::World& Host::world() {
    return game_->World();
}

ApplicationState& Host::application_state() {
    return *app_state_;
}

Time& Host::time() {
    return *time_;
}

void Host::write_window_size(int width, int height, bool send_event) {
    ecs::World& world_ref = world();
    ui::WindowSize& size = world_ref.ctx<ui::WindowSize>();
    size.width = width;
    size.height = height;
    if (send_event) {
        ecs::EventWriter<ui::WindowResizeEvent>{world_ref}.send(ui::WindowResizeEvent{width, height});
    }
    ui::apply_fill_window(world_ref);
}

}
