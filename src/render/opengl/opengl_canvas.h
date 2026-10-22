#pragma once

#include "opengl_backend.h"
#include "opengl_ui_device.h"
#include "window_canvas.h"
#include "window_system.h"

#include "ui/render/ui_renderer.h"

#include <engine/render/command_buffer.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/font.h>

#include <glm/vec2.hpp>

#include <memory>

namespace engine::render {

class OpenGLCanvas final : public IWindowCanvas {
public:
    OpenGLCanvas(WindowSystem& window, CommandBuffer& commands, IRenderBackend& backend);
    ~OpenGLCanvas() override;

    OpenGLCanvas(const OpenGLCanvas&) = delete;
    OpenGLCanvas& operator=(const OpenGLCanvas&) = delete;

    // Configures SDL_GL_* window attributes before SDL_CreateWindow and returns SDL_WINDOW_OPENGL.
    [[nodiscard]] SDL_WindowFlags prepare_window(const WindowDesc& desc) override;

    // with_ui_painter=false makes no UI painter for this canvas (a window whose buffer only ever gets CmdDrawMesh,
    // never CmdDrawUI). Every real window passes the default `true`: each OpenGLCanvas owns its own painter, a
    // UiRenderer over the backend's shared PaintResources with this context's OpenGLUiDevice, and draw() re-arms
    // the one shared OpenGLRenderBackend::ui_painter_ pointer to *this* canvas's painter immediately before its own
    // execute() call.
    [[nodiscard]] bool init(bool with_ui_painter = true) override;
    [[nodiscard]] bool load_ui_font(const Font& font) override;
    [[nodiscard]] bool add_font(AssetId id, const Font& font) override;
    [[nodiscard]] bool add_image(AssetId id, const TextureDesc& desc) override;
    // Drops the registered UI images (PaintResources::drop_images: shared by every window, so every window's
    // images; fonts stay registered). The next frame registers what its canvases reference again.
    [[nodiscard]] bool reset_ui_painter() override;
    void make_current() override;
    // Sets this context's swap interval: vsync on (adaptive where the driver has it, else plain) or off. False
    // without a context or when the driver refuses the interval.
    [[nodiscard]] bool set_vsync(bool on) override;
    [[nodiscard]] ui::IUiPainter* ui_painter() const noexcept override {
        return ui_renderer_.get();
    }
    // render() then present().
    void draw() override;
    // Clears and executes this window's command buffer into its back buffer. Does not swap.
    void render() override;
    // This window's back buffer as render() left it (render::framebuffer_image). Empty without a window or
    // context, or while the window is hidden or minimized.
    [[nodiscard]] TextureDesc read_pixels() override;
    // Swaps this window's buffers.
    void present() override;

    [[nodiscard]] SDL_GLContext native_context() const noexcept {
        return context_;
    }

private:
    void destroy_context();
    void destroy_ui_painter();
    [[nodiscard]] bool create_ui_painter();

    WindowSystem* window_ = nullptr;
    CommandBuffer* commands_ = nullptr;
    IRenderBackend* backend_ = nullptr;
    SDL_GLContext context_ = nullptr;
    std::unique_ptr<OpenGLUiDevice> ui_device_;
    std::unique_ptr<ui::UiRenderer> ui_renderer_;
};

}
