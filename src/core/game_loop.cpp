#include "core/game_loop.h"

#include "cli/cli_server.h"
#include "core/frame_step.h"

#include <engine/core/application_state.h>
#include <engine/core/platform.h>
#include <engine/ecs/world.h>
#include <engine/net/http_client.h>
#include <engine/process/process_launcher.h>
#include <engine/ui/canvas.h>

#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace engine {

int GameLoop::run(IPresentation& presentation, RunHooks hooks, Worlds& worlds, InputSystem& input,
        IAudioSystem* audio, HttpClient* http, ProcessLauncher* processes, std::function<void()> host_dispose) {
    presentation_ = &presentation;
    hooks_ = std::move(hooks);
    worlds_ = &worlds;
    input_ = &input;
    audio_ = audio;
    http_ = http;
    processes_ = processes;
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
    if (hooks_.on_start) {
        hooks_.on_start();
    }
    worlds_->each([](ecs::World& world, FixedStepClock&, bool, bool ui) {
        if (ui) {
            ui::apply_canvas_fit(world);
        }
    });
    worlds_->application_state().running = true;
    cli::start(hooks_.cli.kind);
}

void GameLoop::tick() {
    if (worlds_ == nullptr || input_ == nullptr || presentation_ == nullptr) {
        return;
    }
    const float real_dt = consume_dt();
    cli::CliFrame cli_frame = this->cli_frame();
    cli::begin_frame(cli_frame);
    flush_worlds(*worlds_);
    presentation_->poll(*worlds_, *input_);
    if (http_ != nullptr) {
        http_->poll();
    }
    if (processes_ != nullptr) {
        processes_->poll();
    }
    simulate_worlds(*worlds_, audio_, real_dt);
    presentation_->sync_frame(*worlds_);
    std::vector<FrameCapture> captures = capture_requests();
    presentation_->draw_all(captures);
    cli_frame.captures = captures;
    cli::drain(cli_frame);
    // Last in the frame, so the hook may destroy worlds or rebind windows: nothing of this frame reads
    // them afterwards. reentrant_tick does not call it, because it runs nested inside poll.
    if (hooks_.on_frame_end) {
        hooks_.on_frame_end();
    }
}

void GameLoop::reentrant_tick() {
    // Nested inside SDL_PollEvent while Windows runs its modal move/size loop. Same simulation
    // slice as tick(), without flush or poll: the outer tick already flushed, and flushing
    // again would drop events the outer frame's systems have not read yet. real_dt shares `last_`
    // with tick(), so the frame that resumes after the drag does not replay the whole drag.
    // RunHooks::on_frame_end is not called here: the outer tick is still inside poll, so a hook
    // that destroys a world would pull it out from under that frame.
    if (worlds_ == nullptr || presentation_ == nullptr) {
        return;
    }
    const float real_dt = consume_dt();
    cli::CliFrame cli_frame = this->cli_frame();
    cli::begin_frame(cli_frame);
    simulate_worlds(*worlds_, audio_, real_dt);
    presentation_->sync_frame(*worlds_);
    std::vector<FrameCapture> captures = capture_requests();
    presentation_->draw_all(captures);
    cli_frame.captures = captures;
    cli::drain(cli_frame);
}

cli::CliFrame GameLoop::cli_frame() const {
    return cli::CliFrame{
            .world_for = [worlds = worlds_](WindowId window) { return worlds->world_for(window); },
            .host = &hooks_.cli,
    };
}

std::vector<FrameCapture> GameLoop::capture_requests() {
    std::vector<FrameCapture> captures;
    for (const WindowId window : cli::capture_requests()) {
        captures.push_back(FrameCapture{.window = window, .image = {}});
    }
    return captures;
}

void GameLoop::end() {
    cli::stop();
    if (worlds_ != nullptr && presentation_ != nullptr) {
        presentation_->detach_loop(*worlds_);
    }
    worlds_ = nullptr;
    input_ = nullptr;
    audio_ = nullptr;
    http_ = nullptr;
    processes_ = nullptr;

    const std::function<void()> on_quit = std::move(hooks_.on_quit);
    hooks_ = RunHooks{};
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
