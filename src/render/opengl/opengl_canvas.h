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
    // real window — primary or secondary — passes the default `true` today (SDD §21.6): each
    // OpenGLCanvas owns its own NanoVgPainter, and draw() re-arms the one shared
    // OpenGLRenderBackend::ui_painter_ pointer to *this* canvas's painter immediately before its
    // own execute() call, so "last window's draw() wins" is scoped to that single instant rather
    // than being a permanent, corrupting registration.
    [[nodiscard]] bool init(bool with_ui_painter = true);
    [[nodiscard]] bool load_ui_font(const Font& font);
    [[nodiscard]] bool add_font(AssetId id, const Font& font);
    [[nodiscard]] bool add_image(AssetId id, const TextureDesc& desc);
    void draw() override;

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
