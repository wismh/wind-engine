#pragma once

#include "opengl_backend.h"
#include "nanovg_painter.h"
#include "window_system.h"

#include <engine/render/canvas.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/font.h>

#include <glm/vec2.hpp>

#include <memory>

namespace engine::render {

class OpenGLCanvas final : public ICanvas {
public:
    OpenGLCanvas(WindowSystem& window, CommandBuffer& commands, IRenderBackend& backend);
    ~OpenGLCanvas() override;

    OpenGLCanvas(const OpenGLCanvas&) = delete;
    OpenGLCanvas& operator=(const OpenGLCanvas&) = delete;

    // with_ui_painter=false skips constructing a NanoVgPainter for this canvas entirely (used by
    // callers that only ever push CmdDrawMesh, never CmdDrawUI, into this window's buffer). Every
    // real window — primary or secondary — passes the default `true` today: each
    // OpenGLCanvas owns its own NanoVgPainter, and draw() re-arms the one shared
    // OpenGLRenderBackend::ui_painter_ pointer to *this* canvas's painter immediately before its
    // own execute() call, so "last window's draw() wins" is scoped to that single instant rather
    // than being a permanent, corrupting registration.
    [[nodiscard]] bool init(bool with_ui_painter = true);
    [[nodiscard]] bool load_ui_font(const Font& font);
    [[nodiscard]] bool add_font(AssetId id, const Font& font);
    [[nodiscard]] bool add_image(AssetId id, const TextureDesc& desc);
    // Drops every font and image this canvas's NanoVG context holds and starts a fresh context. The
    // next frame registers what its canvases reference again, the builtin UI font included.
    [[nodiscard]] bool reset_ui_painter();
    void make_current();
    // Sets this context's swap interval: vsync on (adaptive where the driver has it, else plain) or off. False
    // without a context or when the driver refuses the interval.
    [[nodiscard]] bool set_vsync(bool on);
    [[nodiscard]] ui::IUiPainter* ui_painter() const noexcept {
        return ui_painter_.get();
    }
    // render() then present().
    void draw() override;
    // Clears and executes this window's command buffer into its back buffer. Does not swap.
    void render();
    // This window's back buffer as render() left it (render::framebuffer_image). Empty without a window or
    // context, or while the window is hidden or minimized.
    [[nodiscard]] TextureDesc read_pixels();
    // Swaps this window's buffers.
    void present();

    [[nodiscard]] SDL_GLContext native_context() const noexcept {
        return context_;
    }

private:
    void destroy_context();

    WindowSystem* window_ = nullptr;
    CommandBuffer* commands_ = nullptr;
    IRenderBackend* backend_ = nullptr;
    SDL_GLContext context_ = nullptr;
    std::unique_ptr<NanoVgPainter> ui_painter_;
};

}
