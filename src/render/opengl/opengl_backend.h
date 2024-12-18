#pragma once

#include <engine/render/backend.h>

namespace engine::ui {
class IUiPainter;
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
    void execute(const CommandBuffer& commands) override;

    void execute_draw_particles(const CmdDrawParticles& cmd);

private:
    void init_particle_buffers();

    ui::IUiPainter* ui_painter_ = nullptr;
    unsigned int particle_vao_ = 0;
    unsigned int particle_quad_vbo_ = 0;
    unsigned int particle_instance_vbo_ = 0;
    unsigned int default_white_texture_ = 0;
    std::shared_ptr<OpenGLShader> particle_shader_;
    bool particle_buffers_initialized_ = false;
};

}
