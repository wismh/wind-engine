#include <engine/core/engine_runtime.h>

#include "core/game_loop.h"
#include "render/opengl/sdl_gl_presentation.h"
#if !defined(__EMSCRIPTEN__)
#include "render/vulkan/sdl_vk_presentation.h"
#endif

#include <engine/core/platform.h>
#include <engine/resources/font.h>

#include <cstdlib>
#include <cstring>

namespace engine {

namespace {

std::unique_ptr<IPresentation> make_default_presentation() {
#if !defined(__EMSCRIPTEN__)
    if (const char* env = std::getenv("WIND_RENDER_BACKEND")) {
        if (std::strcmp(env, "vulkan") == 0) {
            return make_sdl_vk_presentation();
        }
        if (std::strcmp(env, "opengl") == 0) {
            return make_sdl_gl_presentation();
        }
    }
#if defined(ENGINE_WITH_VULKAN)
    return make_sdl_vk_presentation();
#endif
#endif
    return make_sdl_gl_presentation();
}

} // namespace

struct EngineRuntime::Impl {
    explicit Impl(std::unique_ptr<IPresentation> p) : presentation(std::move(p)) {}

    std::unique_ptr<IPresentation> presentation;
    GameLoop loop;
};

EngineRuntime::EngineRuntime() : EngineRuntime(make_default_presentation()) {}

EngineRuntime::EngineRuntime(std::unique_ptr<IPresentation> presentation)
    : impl_(std::make_unique<Impl>(std::move(presentation))) {}

EngineRuntime::~EngineRuntime() {
    shutdown();
}

bool EngineRuntime::init_video() {
    return impl_->presentation->init_video();
}

bool EngineRuntime::create_window(const WindowDesc& desc) {
    return impl_->presentation->create_primary(desc);
}

void EngineRuntime::set_window_icon(const render::TextureDesc& desc) {
    impl_->presentation->set_icon(desc);
}

bool EngineRuntime::add_font_for_window(WindowId id, AssetId asset, const Font& font) {
    return impl_->presentation->add_font(id, asset, font);
}

bool EngineRuntime::add_image_for_window(WindowId id, AssetId asset, const render::TextureDesc& desc) {
    return impl_->presentation->add_image(id, asset, desc);
}

bool EngineRuntime::reset_ui_cache(WindowId id) {
    return impl_->presentation->reset_ui_cache(id);
}

void EngineRuntime::shutdown() {
    if (impl_ == nullptr || impl_->presentation == nullptr) {
        return;
    }
    impl_->presentation->shutdown();
}

int EngineRuntime::run(RunHooks hooks, Worlds& worlds, InputSystem& input, IAudioSystem* audio, HttpClient* http,
        ProcessLauncher* processes, std::function<void()> host_dispose) {
    return impl_->loop.run(*impl_->presentation, std::move(hooks), worlds, input, audio, http, processes,
            std::move(host_dispose));
}

render::CommandBuffer& EngineRuntime::commands() {
    return impl_->presentation->commands();
}

render::ICanvas& EngineRuntime::canvas() {
    return impl_->presentation->canvas();
}

render::CommandBuffer* EngineRuntime::commands_for_window(WindowId id) {
    return impl_->presentation->commands_for(id);
}

render::IGraphicFactory& EngineRuntime::factory() {
    return impl_->presentation->factory();
}

render::IRenderBackend& EngineRuntime::backend() {
    return impl_->presentation->backend();
}

IWindowControl& EngineRuntime::window_control() {
    return impl_->presentation->window_control();
}

void* EngineRuntime::native_window() const {
    return impl_->presentation->native_window();
}

std::filesystem::path EngineRuntime::base_path() const {
    return impl_->presentation->base_path();
}

std::filesystem::path EngineRuntime::assets_root() const {
    return runtime_assets_root(base_path());
}

void EngineRuntime::write_window_size(Worlds& worlds, bool send_event) {
    impl_->presentation->publish_primary_size(worlds, send_event);
}

}
