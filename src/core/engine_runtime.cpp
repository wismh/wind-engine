#include <engine/core/engine_runtime.h>

#include "core/game_loop.h"
#include "render/opengl/sdl_gl_presentation.h"

#include <engine/core/platform.h>
#include <engine/resources/font.h>

namespace engine {

struct EngineRuntime::Impl {
    std::unique_ptr<IPresentation> presentation = make_sdl_gl_presentation();
    GameLoop loop;
};

EngineRuntime::EngineRuntime() : impl_(std::make_unique<Impl>()) {}

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

void EngineRuntime::shutdown() {
    if (impl_ == nullptr || impl_->presentation == nullptr) {
        return;
    }
    impl_->presentation->shutdown();
}

int EngineRuntime::run(IGame& game, InputSystem& input, IAudioSystem* audio, std::function<void()> host_dispose) {
    return impl_->loop.run(*impl_->presentation, game, input, audio, std::move(host_dispose));
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

void EngineRuntime::write_window_size(ecs::World& world, bool send_event) {
    impl_->presentation->publish_primary_size(world, send_event);
}

}
