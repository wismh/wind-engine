#pragma once

#include <engine/core/window_desc.h>
#include <engine/render/canvas.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/font.h>

#include <SDL3/SDL.h>

namespace engine::ui {
class IUiPainter;
}

namespace engine::render {

// Per-window graphics surface and UI painter lifecycle owned by WindowManager.
// Decouples SDL window management and event presentation from a specific graphics API (OpenGL, Vulkan).
class IWindowCanvas : public ICanvas {
public:
    ~IWindowCanvas() override = default;

    // Configures graphics-API window attributes before SDL_CreateWindow and returns the required
    // SDL_WindowFlags bit (e.g. SDL_WINDOW_OPENGL, SDL_WINDOW_VULKAN, or 0 for a headless/fake canvas).
    [[nodiscard]] virtual SDL_WindowFlags prepare_window(const WindowDesc& desc) = 0;

    // Initializes the graphics context or swapchain for the created window.
    [[nodiscard]] virtual bool init(bool with_ui_painter = true) = 0;

    [[nodiscard]] virtual bool load_ui_font(const Font& font) = 0;
    [[nodiscard]] virtual bool add_font(AssetId id, const Font& font) = 0;
    [[nodiscard]] virtual bool add_image(AssetId id, const TextureDesc& desc) = 0;
    [[nodiscard]] virtual bool reset_ui_painter() = 0;

    // Activates this window's graphics context on the calling thread when the backend requires one (OpenGL).
    virtual void make_current() = 0;

    [[nodiscard]] virtual bool set_vsync(bool on) = 0;
    [[nodiscard]] virtual ui::IUiPainter* ui_painter() const noexcept = 0;

    // Clears and executes this window's command buffer into its back buffer without presenting.
    virtual void render() = 0;
    // Reads back the window's rendered back buffer before present().
    [[nodiscard]] virtual TextureDesc read_pixels() = 0;
    // Presents/swaps this window's back buffer.
    virtual void present() = 0;
};

}
