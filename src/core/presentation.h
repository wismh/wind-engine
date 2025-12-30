#pragma once

#include <engine/core/application_state.h>
#include <engine/core/window_desc.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>

#include <filesystem>
#include <functional>

#include <glm/vec2.hpp>

namespace engine {

class InputSystem;
class IWindowControl;
struct Font;

namespace ecs {
class World;
}

namespace render {
class CommandBuffer;
class ICanvas;
class IRenderBackend;
}

// Window, input-device, and draw boundary. The game loop calls this; it does not include SDL,
// OpenGL, or NanoVG. A second graphics backend is another implementation plus a factory.
class IPresentation {
public:
    virtual ~IPresentation() = default;

    [[nodiscard]] virtual bool init_video() = 0;
    virtual void shutdown() = 0;
    [[nodiscard]] virtual bool create_primary(const WindowDesc& desc) = 0;
    virtual void set_icon(const render::TextureDesc& desc) = 0;
    [[nodiscard]] virtual void* native_window() const = 0;
    [[nodiscard]] virtual glm::ivec2 drawable_size() const = 0;
    [[nodiscard]] virtual std::filesystem::path base_path() const = 0;

    [[nodiscard]] virtual render::IGraphicFactory& factory() = 0;
    [[nodiscard]] virtual render::IRenderBackend& backend() = 0;
    [[nodiscard]] virtual render::ICanvas& canvas() = 0;
    [[nodiscard]] virtual render::CommandBuffer& commands() = 0;
    [[nodiscard]] virtual render::CommandBuffer* commands_for(WindowId id) = 0;
    [[nodiscard]] virtual IWindowControl& window_control() = 0;

    [[nodiscard]] virtual bool add_font(WindowId id, AssetId asset, const Font& font) = 0;
    [[nodiscard]] virtual bool add_image(WindowId id, AssetId asset, const render::TextureDesc& desc) = 0;

    virtual void poll(ecs::World& world, InputSystem& input, ApplicationState& app) = 0;
    virtual void sync_frame(ecs::World& world) = 0;
    virtual void draw_all() = 0;

    // Publishes the primary drawable size, installs the UI glyph resolver, and arms the
    // desktop-overlay modal hook with `reentrant_tick` for as long as the loop runs.
    virtual void attach_loop(ecs::World& world, std::function<void()> reentrant_tick) = 0;
    virtual void detach_loop(ecs::World& world) = 0;
    virtual void publish_primary_size(ecs::World& world, bool send_event) = 0;
};

}
