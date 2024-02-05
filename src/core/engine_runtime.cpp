#include <engine/core/engine_runtime.h>

#include "render/opengl/opengl_runtime.h"

#include <engine/audio/audio_system.h>
#include <engine/core/fixed_step.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/resources/font.h>
#include <engine/ui/canvas.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace engine {
namespace {

MouseButton mouse_button_from_sdl(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return MouseButton::Left;
        case SDL_BUTTON_MIDDLE:
            return MouseButton::Middle;
        case SDL_BUTTON_RIGHT:
            return MouseButton::Right;
        default:
            return MouseButton::None;
    }
}

}

struct EngineRuntime::Impl {
    WindowSystem window;
    std::shared_ptr<render::CommandBuffer> commands = std::make_shared<render::CommandBuffer>();
    std::shared_ptr<render::OpenGLFactory> factory = std::make_shared<render::OpenGLFactory>();
    std::shared_ptr<render::OpenGLRenderBackend> backend = std::make_shared<render::OpenGLRenderBackend>();
    std::shared_ptr<render::OpenGLCanvas> canvas;
    bool video_inited = false;

    Impl() {
        canvas = std::make_shared<render::OpenGLCanvas>(window, *commands, *backend);
    }
};

EngineRuntime::EngineRuntime() : impl_(std::make_unique<Impl>()) {}

EngineRuntime::~EngineRuntime() {
    shutdown();
}

bool EngineRuntime::init_video() {
    if (impl_->video_inited) {
        return true;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }
    impl_->video_inited = true;
    return true;
}

bool EngineRuntime::create_window(std::string_view title, glm::ivec2 size) {
    if (!impl_->window.create(title, size)) {
        return false;
    }
    return impl_->canvas->init();
}

bool EngineRuntime::load_ui_font(const Font& font) {
    if (impl_->canvas == nullptr) {
        return false;
    }
    return impl_->canvas->load_ui_font(font);
}

bool EngineRuntime::add_font(AssetId id, const Font& font) {
    if (impl_->canvas == nullptr) {
        return false;
    }
    return impl_->canvas->add_font(id, font);
}

void EngineRuntime::shutdown() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->canvas) {
        impl_->canvas.reset();
    }
    impl_->window.destroy();
    if (impl_->video_inited) {
        SDL_Quit();
        impl_->video_inited = false;
    }
    if (impl_->canvas == nullptr && impl_->commands) {
        impl_->canvas = std::make_shared<render::OpenGLCanvas>(impl_->window, *impl_->commands, *impl_->backend);
    }
}

int EngineRuntime::run(IGame& game, InputSystem& input, IAudioSystem* audio) {
    ecs::World& world = game.World();
    Time& time = world.ctx<Time>();
    ApplicationState& app = world.ctx<ApplicationState>();
    FixedStepClock clock(time, app);

    game.OnStart();
    ui::apply_fill_window(world);
    app.running = true;

    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();

    while (app.running) {
        const auto now = Clock::now();
        const float real_dt = std::chrono::duration<float>(now - last).count();
        last = now;

        world.FlushEvents();
        poll_events(world, input, app);
        ui::begin_frame(world);

        const int steps = clock.advance(real_dt);
        if (audio != nullptr) {
            audio->Update(time.deltaTime);
        }
        for (int i = 0; i < steps; ++i) {
            game.OnFixedUpdate();
        }
        game.OnUpdate();
        canvas().Draw();
    }

    game.OnQuit();
    return 0;
}

render::CommandBuffer& EngineRuntime::commands() {
    return *impl_->commands;
}

render::ICanvas& EngineRuntime::canvas() {
    return *impl_->canvas;
}

render::IGraphicFactory& EngineRuntime::factory() {
    return *impl_->factory;
}

render::IRenderBackend& EngineRuntime::backend() {
    return *impl_->backend;
}

std::shared_ptr<render::CommandBuffer> EngineRuntime::commands_ptr() const {
    return impl_->commands;
}

std::shared_ptr<render::ICanvas> EngineRuntime::canvas_ptr() const {
    return impl_->canvas;
}

std::shared_ptr<render::IGraphicFactory> EngineRuntime::factory_ptr() const {
    return impl_->factory;
}

std::shared_ptr<render::IRenderBackend> EngineRuntime::backend_ptr() const {
    return impl_->backend;
}

void* EngineRuntime::native_window() const {
    return impl_->window.window();
}

glm::ivec2 EngineRuntime::drawable_size() const {
    return impl_->window.drawable_size();
}

std::filesystem::path EngineRuntime::base_path() const {
    const char* base = SDL_GetBasePath();
    if (base == nullptr) {
        return {};
    }
    return std::filesystem::path(base);
}

std::filesystem::path EngineRuntime::assets_root() const {
    const std::filesystem::path base = base_path();
    if (base.empty()) {
        return {};
    }
    return base / "assets";
}

void EngineRuntime::write_window_size(ecs::World& world, bool send_event) {
    const glm::ivec2 size = drawable_size();
    ui::WindowSize& ctx = world.ctx<ui::WindowSize>();
    ctx.width = size.x;
    ctx.height = size.y;
    if (send_event) {
        ecs::EventWriter<ui::WindowResizeEvent>{world}.send(ui::WindowResizeEvent{size.x, size.y});
    }
    ui::apply_fill_window(world);
}

void EngineRuntime::poll_events(ecs::World& world, InputSystem& input, ApplicationState& app) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                app.Quit();
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                write_window_size(world, true);
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (!event.key.repeat) {
                    input.handle_key(static_cast<KeyCode>(static_cast<std::uint32_t>(event.key.scancode)),
                            event.key.down);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                input.handle_mouse_button(mouse_button_from_sdl(event.button.button), event.button.down,
                        glm::vec2{event.button.x, event.button.y});
                break;
            case SDL_EVENT_MOUSE_MOTION:
                input.handle_mouse_move(glm::vec2{event.motion.x, event.motion.y},
                        glm::vec2{event.motion.xrel, event.motion.yrel});
                break;
            default:
                break;
        }
    }
}

}
