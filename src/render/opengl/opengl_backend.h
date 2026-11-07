#pragma once

#include "context_vao.h"
#include "opengl_post.h"

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
    // Drawable size and the window clear alpha (0 when the window is transparent). Post targets are sized from it.
    void set_frame_surface(int width, int height, float clear_alpha) noexcept;
    // The fonts, glyph atlas, path masks, gradient ramps, and images every window's UiRenderer draws from: one set
    // for the backend every window shares (docs/tech/architecture/UI Performance Plan.md, step 5). CPU only; each
    // window's device keeps its own textures of it.
    [[nodiscard]] ui::PaintResources& ui_resources() noexcept { return *ui_resources_; }
    void execute(const CommandBuffer& commands) override;

    // The view state the following draws use (CmdSetView). `execute` resets it to a default CmdSetView first.
    void set_view(const CmdSetView& view);
    void finish_post();
    void execute_draw_mesh(const CmdDrawMesh& cmd);
    void execute_draw_decal(const CmdDrawDecal& cmd);
    void execute_draw_particles(const CmdDrawParticles& cmd);
    void execute_draw_mesh_particles(const CmdDrawParticles& cmd);
    // ENGINE_GL_CHECKS: logs the GL errors raised since the last check, once per error and `where`. Otherwise nothing.
    void check_gl_errors(std::string_view where);

private:
    // A 1x1 white RGBA texture, made on first use, shared by every context.
    unsigned int white_texture();
    // A 1x1 white texture array of one layer, made on first use, shared by every context.
    unsigned int white_texture_array();
    // Binds both targets of unit 0: `albedo` on its own target (`GL_TEXTURE_2D` or `GL_TEXTURE_2D_ARRAY`) and white on
    // the other, or white on both when `albedo` is null or invalid. A program reads only the target its sampler type
    // names, so a sampler2D draw with an array albedo (or the reverse) samples white.
    void bind_albedo(const std::shared_ptr<ITexture>& albedo);
    void init_particle_buffers();
    void set_up_particle_vao() const;

    ui::IUiPainter* ui_painter_ = nullptr;
    std::unique_ptr<ui::PaintResources> ui_resources_;
    ContextVao particle_vao_;
    unsigned int particle_quad_vbo_ = 0;
    unsigned int particle_instance_vbo_ = 0;
    unsigned int default_white_texture_ = 0;
    unsigned int default_white_texture_array_ = 0;
    std::shared_ptr<OpenGLShader> particle_shader_;
    bool particle_buffers_initialized_ = false;
    GlErrorLog gl_errors_;
    CmdSetView view_;
    int full_viewport_[4]{0, 0, 0, 0};
    OpenGLPost post_;
};

} // namespace engine::render
