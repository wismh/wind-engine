#include "core/game_loop.h"

#include "core/frame_step.h"

#include <engine/core/application_state.h>
#include <engine/core/platform.h>
#include <engine/core/time.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace engine {

int GameLoop::run(IPresentation& presentation, IGame& game, InputSystem& input, IAudioSystem* audio,
        std::function<void()> host_dispose) {
    presentation_ = &presentation;
    game_ = &game;
    input_ = &input;
    audio_ = audio;
    host_dispose_ = std::move(host_dispose);
    begin();

    const MainLoopPolicy policy{default_loop_kind()};
#if defined(__EMSCRIPTEN__)
    if (policy.uses_request_animation_frame()) {
        emscripten_set_main_loop_arg(&GameLoop::main_loop_thunk, this, 0, 1);
        return 0;
    }
#else
    (void)policy;
#endif

    ApplicationState& app = game_->world().ctx<ApplicationState>();
    while (app.running) {
        tick();
    }
    end();
    return 0;
}

void GameLoop::begin() {
    ecs::World& world = game_->world();
    clock_ = std::make_unique<FixedStepClock>(world.ctx<Time>(), world.ctx<ApplicationState>());
    last_ = std::chrono::steady_clock::now();
    shutdown_ = LoopShutdown{};

    presentation_->attach_loop(world, [this] { reentrant_tick(); });
    game_->on_start();
    ui::apply_canvas_fit(world);
    world.ctx<ApplicationState>().running = true;
}

void GameLoop::tick() {
    if (game_ == nullptr || input_ == nullptr || clock_ == nullptr || presentation_ == nullptr) {
        return;
    }
    const float real_dt = consume_dt();
    ecs::World& world = game_->world();
    flush_game_events(*game_);
    presentation_->poll(world, *input_, world.ctx<ApplicationState>());
    simulate_game_frame(*game_, audio_, *clock_, real_dt);
    presentation_->sync_frame(world);
    presentation_->draw_all();
}

void GameLoop::reentrant_tick() {
    // Nested inside SDL_PollEvent while Windows runs its modal move/size loop. Same simulation
    // slice as tick(), without flush_events or poll: the outer tick already flushed, and flushing
    // again would drop events the outer frame's systems have not read yet. real_dt shares `last_`
    // with tick(), so the frame that resumes after the drag does not replay the whole drag.
    if (game_ == nullptr || clock_ == nullptr || presentation_ == nullptr) {
        return;
    }
    const float real_dt = consume_dt();
    simulate_game_frame(*game_, audio_, *clock_, real_dt);
    presentation_->sync_frame(game_->world());
    presentation_->draw_all();
}

void GameLoop::end() {
    IGame* const game = game_;
    if (game != nullptr && presentation_ != nullptr) {
        presentation_->detach_loop(game->world());
    }
    game_ = nullptr;
    input_ = nullptr;
    audio_ = nullptr;
    clock_.reset();

    const std::function<void()> on_quit = game == nullptr ? std::function<void()>{}
                                                          : std::function<void()>{[game] { game->on_quit(); }};
    shutdown_.complete(on_quit, host_dispose_);
}

float GameLoop::consume_dt() {
    const auto now = std::chrono::steady_clock::now();
    const float real_dt = std::chrono::duration<float>(now - last_).count();
    last_ = now;
    return real_dt;
}

void GameLoop::main_loop_thunk(void* self) {
    auto* loop = static_cast<GameLoop*>(self);
    if (loop == nullptr || loop->game_ == nullptr) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    loop->tick();
    if (loop->game_ == nullptr) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    if (!loop->game_->world().ctx<ApplicationState>().running) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        loop->end();
    }
}

}
