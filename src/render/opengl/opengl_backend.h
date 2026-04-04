#pragma once

#include "context_vao.h"

#include "render/gl_error_log.h"

#include <engine/render/backend.h>
#include <engine/render/commands.h>

#include <memory>
#include <string_view>

namespace engine::ui {
class IUiPainter;
struct PaintResources;
}

namespace engine::render {

class OpenGLShader;

class OpenGLRenderBackend final : public IRenderBackend {
public:
    OpenGLRenderBackend();
    ~OpenGLRenderBackend() override;

    OpenGLRenderBackend(const OpenGLRenderBackend&) = delete;
    OpenGLRenderBackend& operator=(const OpenGLRenderBackend&) = delete;

    void set_ui_painter(ui::IUiPainter* painter);
    // The fonts, glyph atlas, path masks, gradient ramps, and images every window's UiRenderer draws from: one set
    // for the backend every window shares (docs/tech/architecture/UI Performance Plan.md, step 5). CPU only; each
    // window's device keeps its own textures of it.
    [[nodiscard]] ui::PaintResources& ui_resources() noexcept { return *ui_resources_; }
    void execute(const CommandBuffer& commands) override;

    // The view state the following draws use (CmdSetView). `execute` resets it to a default CmdSetView first.
    void set_view(const CmdSetView& view);
    void execute_draw_mesh(const CmdDrawMesh& cmd);
    void execute_draw_particles(const CmdDrawParticles& cmd);
    // ENGINE_GL_CHECKS: logs the GL errors raised since the last check, once per error and `where`. Otherwise nothing.
    void check_gl_errors(std::string_view where);

private:
    // A 1x1 white RGBA texture, made on first use, shared by every context.
    unsigned int white_texture();
    // The material's albedo on unit 0, or the white texture when it has none.
    void bind_albedo(const IMaterial& material);
    void init_particle_buffers();
    void set_up_particle_vao() const;

    ui::IUiPainter* ui_painter_ = nullptr;
    std::unique_ptr<ui::PaintResources> ui_resources_;
    ContextVao particle_vao_;
    unsigned int particle_quad_vbo_ = 0;
    unsigned int particle_instance_vbo_ = 0;
    unsigned int default_white_texture_ = 0;
    std::shared_ptr<OpenGLShader> particle_shader_;
    bool particle_buffers_initialized_ = false;
    GlErrorLog gl_errors_;
    CmdSetView view_;
};

}
