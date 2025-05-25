#include "core/game_loop.h"

#include "cli/cli_server.h"
#include "core/frame_step.h"

#include <engine/core/application_state.h>
#include <engine/core/platform.h>
#include <engine/ecs/world.h>
#include <engine/igame.h>
#include <engine/ui/canvas.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace engine {

int GameLoop::run(IPresentation& presentation, IGame& game, Worlds& worlds, InputSystem& input, IAudioSystem* audio,
        std::function<void()> host_dispose) {
    presentation_ = &presentation;
    game_ = &game;
    worlds_ = &worlds;
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

    ApplicationState& app = worlds_->application_state();
    while (app.running) {
        tick();
    }
    end();
    return 0;
}

void GameLoop::begin() {
    last_ = std::chrono::steady_clock::now();
    shutdown_ = LoopShutdown{};

    presentation_->attach_loop(*worlds_, [this] { reentrant_tick(); });
    game_->on_start();
    worlds_->each([](ecs::World& world, FixedStepClock&, bool, bool ui) {
        if (ui) {
            ui::apply_canvas_fit(world);
        }
    });
    worlds_->application_state().running = true;
    cli::start();
}

void GameLoop::tick() {
    if (game_ == nullptr || worlds_ == nullptr || input_ == nullptr || presentation_ == nullptr) {
        return;
    }
    const float real_dt = consume_dt();
    ecs::World* const primary = worlds_->world_for(kPrimaryWindow);
    if (primary != nullptr) {
        cli::begin_frame(*primary);
    }
    flush_worlds(*worlds_);
    presentation_->poll(*worlds_, *input_);
    simulate_worlds(*worlds_, audio_, real_dt);
    presentation_->sync_frame(*worlds_);
    presentation_->draw_all();
    if (primary != nullptr) {
        cli::drain(*primary);
    }
}

void GameLoop::reentrant_tick() {
    // Nested inside SDL_PollEvent while Windows runs its modal move/size loop. Same simulation
    // slice as tick(), without flush or poll: the outer tick already flushed, and flushing
    // again would drop events the outer frame's systems have not read yet. real_dt shares `last_`
    // with tick(), so the frame that resumes after the drag does not replay the whole drag.
    if (worlds_ == nullptr || presentation_ == nullptr) {
        return;
    }
    const float real_dt = consume_dt();
    ecs::World* const primary = worlds_->world_for(kPrimaryWindow);
    if (primary != nullptr) {
        cli::begin_frame(*primary);
    }
    simulate_worlds(*worlds_, audio_, real_dt);
    presentation_->sync_frame(*worlds_);
    presentation_->draw_all();
    if (primary != nullptr) {
        cli::drain(*primary);
    }
}

void GameLoop::end() {
    cli::stop();
    IGame* const game = game_;
    if (worlds_ != nullptr && presentation_ != nullptr) {
        presentation_->detach_loop(*worlds_);
    }
    game_ = nullptr;
    worlds_ = nullptr;
    input_ = nullptr;
    audio_ = nullptr;

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
    if (loop == nullptr || loop->worlds_ == nullptr) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    loop->tick();
    if (loop->worlds_ == nullptr) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    if (!loop->worlds_->application_state().running) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        loop->end();
    }
}

}
