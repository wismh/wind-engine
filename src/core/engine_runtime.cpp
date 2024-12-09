#include <engine/core/engine_runtime.h>

#include "render/opengl/opengl_runtime.h"

#include <engine/audio/audio_system.h>
#include <engine/core/app_lifecycle.h>
#include <engine/core/fixed_step.h>
#include <engine/core/key_code.h>
#include <engine/core/platform.h>
#include <engine/core/time.h>
#include <engine/core/web_loop.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/resources/font.h>
#include <engine/resources/meta.h>
#include <engine/ui/canvas.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

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

#if defined(__ANDROID__)
bool copy_sdl_io_file(const char* sdl_path, const std::filesystem::path& dest) {
    SDL_IOStream* io = SDL_IOFromFile(sdl_path, "rb");
    if (io == nullptr) {
        return false;
    }
    const Sint64 size = SDL_GetIOSize(io);
    if (size < 0) {
        SDL_CloseIO(io);
        return false;
    }
    std::vector<char> buf(static_cast<std::size_t>(size));
    if (size > 0 && SDL_ReadIO(io, buf.data(), static_cast<std::size_t>(size)) != static_cast<std::size_t>(size)) {
        SDL_CloseIO(io);
        return false;
    }
    SDL_CloseIO(io);
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) {
        return false;
    }
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        return false;
    }
    if (!buf.empty()) {
        out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    }
    return static_cast<bool>(out);
}

// This SDL3 build has no Android-specific SDL_EnumerateDirectory/SDL_GetPathInfo backend
// (see external/SDL3/src/filesystem/android/SDL_sysfilesystem.c — only GetBasePath/GetPrefPath
// are implemented there), so directory enumeration over the packaged "assets://" tree never
// finds anything on Android; it silently walks zero entries. SDL_IOFromFile() by exact name
// does reach the APK's AAssetManager, though. So instead of enumerating, stage each asset the
// cooked catalog already lists by its known relative path.
void stage_catalog_assets(
        const std::filesystem::path& catalog_file, const std::string& sdl_prefix, const std::filesystem::path& dest_root) {
    std::ifstream in(catalog_file, std::ios::binary);
    if (!in) {
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsed = parse_cooked_catalog(text);
    if (!parsed) {
        return;
    }
    for (const CatalogEntry& entry : parsed->entries()) {
        copy_sdl_io_file((sdl_prefix + entry.relative_path).c_str(), dest_root / entry.relative_path);
    }
}

std::filesystem::path android_runtime_assets_root(const std::filesystem::path& base) {
    const char* storage = SDL_GetAndroidInternalStoragePath();
    const std::filesystem::path internal = storage != nullptr ? std::filesystem::path{storage} : std::filesystem::path{};
    const std::filesystem::path dest = default_assets_root(internal, Platform::Android);

    std::error_code ec;
    if (std::filesystem::exists(dest / "engine" / "catalog.toml", ec)) {
        return dest;
    }

    const std::string generic = base.generic_string();
    if (!base.empty() && generic.find("assets:") == std::string::npos && generic != "." && generic != "./") {
        const std::filesystem::path src = default_assets_root(base, Platform::Native);
        if (std::filesystem::is_directory(src, ec)) {
            stage_android_assets(src, dest);
            return dest;
        }
    }

    copy_sdl_io_file("catalog.toml", dest / "catalog.toml");
    copy_sdl_io_file("engine/catalog.toml", dest / "engine" / "catalog.toml");
    stage_catalog_assets(dest / "catalog.toml", "", dest);
    stage_catalog_assets(dest / "engine" / "catalog.toml", "engine/", dest / "engine");
    return dest;
}
#endif

}

struct EngineRuntime::Impl {
    WindowSystem window;
    std::shared_ptr<render::CommandBuffer> commands = std::make_shared<render::CommandBuffer>();
    std::shared_ptr<render::OpenGLFactory> factory = std::make_shared<render::OpenGLFactory>();
    std::shared_ptr<render::OpenGLRenderBackend> backend = std::make_shared<render::OpenGLRenderBackend>();
    std::shared_ptr<render::OpenGLCanvas> canvas;
    bool video_inited = false;
    IGame* loop_game = nullptr;
    InputSystem* loop_input = nullptr;
    IAudioSystem* loop_audio = nullptr;
    std::unique_ptr<FixedStepClock> loop_clock;
    std::chrono::steady_clock::time_point loop_last{};
    std::function<void()> host_dispose;
    LoopShutdown loop_shutdown;

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
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
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

bool EngineRuntime::add_image(AssetId id, const render::TextureDesc& desc) {
    if (impl_->canvas == nullptr) {
        return false;
    }
    return impl_->canvas->add_image(id, desc);
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

int EngineRuntime::run(IGame& game, InputSystem& input, IAudioSystem* audio, std::function<void()> host_dispose) {
    impl_->host_dispose = std::move(host_dispose);
    begin_loop(game, input, audio);

    const MainLoopPolicy policy{default_loop_kind()};
#if defined(__EMSCRIPTEN__)
    if (policy.uses_request_animation_frame()) {
        emscripten_set_main_loop_arg(&EngineRuntime::main_loop_thunk, this, 0, 1);
        return 0;
    }
#else
    (void)policy;
#endif

    ApplicationState& app = game.world().ctx<ApplicationState>();
    while (app.running) {
        tick_loop();
    }
    end_loop();
    return 0;
}

void EngineRuntime::begin_loop(IGame& game, InputSystem& input, IAudioSystem* audio) {
    impl_->loop_game = &game;
    impl_->loop_input = &input;
    impl_->loop_audio = audio;
    impl_->loop_clock = std::make_unique<FixedStepClock>(game.world().ctx<Time>(), game.world().ctx<ApplicationState>());
    impl_->loop_last = std::chrono::steady_clock::now();
    impl_->loop_shutdown = LoopShutdown{};

    game.on_start();
    ui::apply_fill_window(game.world());
    game.world().ctx<ApplicationState>().running = true;
}

void EngineRuntime::tick_loop() {
    if (impl_->loop_game == nullptr || impl_->loop_input == nullptr || impl_->loop_clock == nullptr) {
        return;
    }
    IGame& game = *impl_->loop_game;
    ecs::World& world = game.world();
    Time& time = world.ctx<Time>();
    ApplicationState& app = world.ctx<ApplicationState>();

    const auto now = std::chrono::steady_clock::now();
    const float real_dt = std::chrono::duration<float>(now - impl_->loop_last).count();
    impl_->loop_last = now;

    world.flush_events();
    poll_events(world, *impl_->loop_input, app);
    ui::begin_frame(world);

    const int steps = impl_->loop_clock->advance(real_dt);
    if (impl_->loop_audio != nullptr) {
        impl_->loop_audio->update(time.delta_time);
    }
    for (int i = 0; i < steps; ++i) {
        game.on_fixed_update();
    }
    game.on_update();
    canvas().draw();
}

void EngineRuntime::end_loop() {
    IGame* const game = impl_->loop_game;
    impl_->loop_game = nullptr;
    impl_->loop_input = nullptr;
    impl_->loop_audio = nullptr;
    impl_->loop_clock.reset();

    const std::function<void()> on_quit = game == nullptr ? std::function<void()>{}
                                                          : std::function<void()>{[game] { game->on_quit(); }};
    impl_->loop_shutdown.complete(on_quit, impl_->host_dispose);
}

void EngineRuntime::main_loop_thunk(void* self) {
    auto* runtime = static_cast<EngineRuntime*>(self);
    if (runtime == nullptr || runtime->impl_ == nullptr) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    runtime->tick_loop();
    if (runtime->impl_->loop_game == nullptr) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    if (!runtime->impl_->loop_game->world().ctx<ApplicationState>().running) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        runtime->end_loop();
    }
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
#if defined(__ANDROID__)
    return android_runtime_assets_root(base_path());
#endif
    // Do not drop an empty SDL_GetBasePath(): on web that still maps to /assets
    // (SDD-WIND-WEB-001 §5). Native empty base stays empty via the helper.
    return default_assets_root(base_path());
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
                app.quit();
                break;
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
            case SDL_EVENT_DID_ENTER_BACKGROUND:
                apply_app_lifecycle(app, AppLifecycleEvent::WillEnterBackground);
                break;
            case SDL_EVENT_WILL_ENTER_FOREGROUND:
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                apply_app_lifecycle(app, AppLifecycleEvent::DidEnterForeground);
                break;
            case SDL_EVENT_TERMINATING:
                apply_app_lifecycle(app, AppLifecycleEvent::Terminating);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                write_window_size(world, true);
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (!event.key.repeat) {
                    const auto code = static_cast<KeyCode>(static_cast<std::uint32_t>(event.key.scancode));
                    input.handle_key(code, event.key.down);
                    if (event.key.down && code == KeyCode::AcBack) {
                        apply_android_back(app);
                    }
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
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_UP: {
                const glm::ivec2 size = drawable_size();
                const glm::vec2 pos = denormalize_touch({event.tfinger.x, event.tfinger.y}, size);
                input.handle_touch(static_cast<std::uint32_t>(event.tfinger.fingerID),
                        event.type == SDL_EVENT_FINGER_DOWN, pos);
                break;
            }
            case SDL_EVENT_FINGER_MOTION: {
                const glm::ivec2 size = drawable_size();
                const glm::vec2 pos = denormalize_touch({event.tfinger.x, event.tfinger.y}, size);
                const glm::vec2 rel = denormalize_touch({event.tfinger.dx, event.tfinger.dy}, size);
                input.handle_touch_move(static_cast<std::uint32_t>(event.tfinger.fingerID), pos, rel);
                break;
            }
            default:
                break;
        }
    }
}

}
