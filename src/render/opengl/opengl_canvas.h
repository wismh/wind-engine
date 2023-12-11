#pragma once

#include "opengl_backend.h"
#include "nanovg_painter.h"
#include "window_system.h"

#include <engine/render/canvas.h>
#include <engine/render/command_buffer.h>
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

    [[nodiscard]] bool init();
    [[nodiscard]] bool load_ui_font(const Font& font);
    void Draw() override;

private:
    void destroy_context();

    WindowSystem* window_ = nullptr;
    CommandBuffer* commands_ = nullptr;
    IRenderBackend* backend_ = nullptr;
    SDL_GLContext context_ = nullptr;
    std::unique_ptr<NanoVgPainter> ui_painter_;
};

}
