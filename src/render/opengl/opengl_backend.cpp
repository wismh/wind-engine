#include "opengl_backend.h"

#include "gl_includes.h"
#include "opengl_mesh.h"
#include "opengl_shader.h"
#include "opengl_texture.h"

#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/render/material.h>
#include <engine/render/shader_adapt.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace engine::render {
namespace {

constexpr std::string_view kParticleVert = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aInstancePos;
layout(location = 3) in float aInstanceRot;
layout(location = 4) in vec2 aInstanceSize;
layout(location = 5) in vec4 aInstanceColor;
layout(location = 6) in vec2 aInstanceUvScale;
layout(location = 7) in vec2 aInstanceUvOffset;

out vec2 vUV;
out vec4 vColor;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uColor;

void main() {
    vUV = aUV * aInstanceUvScale + aInstanceUvOffset;
    vColor = aInstanceColor * uColor;

    float cos_r = cos(aInstanceRot);
    float sin_r = sin(aInstanceRot);
    vec2 scaled = aPosition.xy * aInstanceSize;
    vec2 rotated = vec2(
        scaled.x * cos_r - scaled.y * sin_r,
        scaled.x * sin_r + scaled.y * cos_r
    );

    vec3 world_pos = aInstancePos + vec3(rotated, aPosition.z);
    gl_Position = uProjection * uView * vec4(world_pos, 1.0);
}
)";

constexpr std::string_view kParticleFrag = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;

uniform sampler2D uTexture;

void main() {
    FragColor = texture(uTexture, vUV) * vColor;
}
)";

struct QuadVertex {
    float x, y, z;
    float u, v;
};

void apply_blend(BlendMode blend) {
    switch (blend) {
        case BlendMode::Opaque:
            glDisable(GL_BLEND);
            break;
        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
    }
}

void execute_draw_mesh(const CmdDrawMesh& cmd) {
    if (!cmd.mesh || !cmd.material) {
        return;
    }

    apply_blend(cmd.material->blend());

    const auto shader = std::dynamic_pointer_cast<OpenGLShader>(cmd.material->shader());
    const auto mesh = std::dynamic_pointer_cast<OpenGLMesh>(cmd.mesh);
    if (!shader || !mesh || !shader->valid() || !mesh->valid()) {
        return;
    }

    shader->use();
    shader->set_mat4("uModel", cmd.model);
    shader->set_mat4("uView", cmd.view);
    shader->set_mat4("uProjection", cmd.projection);
    shader->set_vec4("uColor", cmd.material->color() * cmd.color);
    shader->set_vec2("uUvScale", cmd.uv_scale);
    shader->set_vec2("uUvOffset", cmd.uv_offset);
    shader->set_int("uTexture", 0);

    if (const auto texture = std::dynamic_pointer_cast<OpenGLTexture>(cmd.material->texture(0))) {
        if (texture->valid()) {
            texture->bind(0);
        }
    }

    mesh->draw();
}

struct ExecuteVisitor {
    ui::IUiPainter* painter = nullptr;
    OpenGLRenderBackend* backend = nullptr;

    void operator()(const CmdDrawMesh& cmd) const {
        execute_draw_mesh(cmd);
    }

    void operator()(const CmdDrawUI& cmd) const {
        if (painter == nullptr || cmd.document == nullptr) {
            return;
        }
        ui::paint_document(*cmd.document, cmd.stylesheet, *painter,
                ui::UiPaintInput{
                        .canvas_rect = cmd.rect,
                        .pointer = cmd.pointer,
                        .pointer_down = cmd.pointer_down,
                        .delta_time = cmd.delta_time,
                        .window_width = cmd.window_width,
                        .window_height = cmd.window_height,
                        .ui_offset = cmd.ui_offset,
                        .ui_scale = cmd.ui_scale,
                });
    }

    void operator()(const CmdDrawParticles& cmd) const {
        if (backend != nullptr) {
            backend->execute_draw_particles(cmd);
        }
    }
};

}

OpenGLRenderBackend::OpenGLRenderBackend() = default;

OpenGLRenderBackend::~OpenGLRenderBackend() {
    if (default_white_texture_ != 0) {
        glDeleteTextures(1, &default_white_texture_);
    }
    if (particle_instance_vbo_ != 0) {
        glDeleteBuffers(1, &particle_instance_vbo_);
    }
    if (particle_quad_vbo_ != 0) {
        glDeleteBuffers(1, &particle_quad_vbo_);
    }
    if (particle_vao_ != 0) {
        glDeleteVertexArrays(1, &particle_vao_);
    }
}

void OpenGLRenderBackend::init_particle_buffers() {
    if (particle_buffers_initialized_) {
        return;
    }

#if defined(ENGINE_WITH_GLES)
    const ShaderTarget target = ShaderTarget::Glsl300Es;
#else
    const ShaderTarget target = ShaderTarget::Glsl330Core;
#endif
    const std::string adapted_vert = adapt_glsl(kParticleVert, target, false);
    const std::string adapted_frag = adapt_glsl(kParticleFrag, target, true);
    particle_shader_ = std::make_shared<OpenGLShader>(adapted_vert, adapted_frag);

    glGenTextures(1, &default_white_texture_);
    glBindTexture(GL_TEXTURE_2D, default_white_texture_);
    const std::uint32_t white_pixel = 0xFFFFFFFF;
    glTexImage2D(GL_TEXTURE_2D, 0,
#if defined(ENGINE_WITH_GLES)
            GL_RGBA8,
#else
            GL_RGBA,
#endif
            1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    static constexpr QuadVertex kQuadVertices[6] = {
            {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
            { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
            { 0.5f,  0.5f, 0.0f, 1.0f, 1.0f},
            {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
            { 0.5f,  0.5f, 0.0f, 1.0f, 1.0f},
            {-0.5f,  0.5f, 0.0f, 0.0f, 1.0f},
    };

    glGenVertexArrays(1, &particle_vao_);
    glBindVertexArray(particle_vao_);

    glGenBuffers(1, &particle_quad_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, particle_quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<const void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
            reinterpret_cast<const void*>(offsetof(QuadVertex, u)));

    glGenBuffers(1, &particle_instance_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, particle_instance_vbo_);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance),
            reinterpret_cast<const void*>(offsetof(ParticleInstance, position)));
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance),
            reinterpret_cast<const void*>(offsetof(ParticleInstance, rotation)));
    glVertexAttribDivisor(3, 1);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance),
            reinterpret_cast<const void*>(offsetof(ParticleInstance, size)));
    glVertexAttribDivisor(4, 1);

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance),
            reinterpret_cast<const void*>(offsetof(ParticleInstance, color)));
    glVertexAttribDivisor(5, 1);

    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance),
            reinterpret_cast<const void*>(offsetof(ParticleInstance, uv_scale)));
    glVertexAttribDivisor(6, 1);

    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance),
            reinterpret_cast<const void*>(offsetof(ParticleInstance, uv_offset)));
    glVertexAttribDivisor(7, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    particle_buffers_initialized_ = true;
}

void OpenGLRenderBackend::execute_draw_particles(const CmdDrawParticles& cmd) {
    if (cmd.instances.empty()) {
        return;
    }

    apply_blend(cmd.blend);

    init_particle_buffers();
    if (particle_vao_ == 0 || !particle_shader_ || !particle_shader_->valid()) {
        return;
    }

    std::shared_ptr<OpenGLShader> active_shader = particle_shader_;
    if (cmd.material) {
        if (auto custom = std::dynamic_pointer_cast<OpenGLShader>(cmd.material->shader())) {
            if (custom->valid() && custom->uniform_location("uModel") == -1) {
                active_shader = custom;
            }
        }
    }

    active_shader->use();
    active_shader->set_mat4("uView", cmd.view);
    active_shader->set_mat4("uProjection", cmd.projection);

    const glm::vec4 material_color = cmd.material ? cmd.material->color() : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
    active_shader->set_vec4("uColor", material_color);
    active_shader->set_int("uTexture", 0);

    bool bound_texture = false;
    if (cmd.material) {
        if (const auto texture = std::dynamic_pointer_cast<OpenGLTexture>(cmd.material->texture(0))) {
            if (texture->valid()) {
                texture->bind(0);
                bound_texture = true;
            }
        }
    }

    if (!bound_texture && default_white_texture_ != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, default_white_texture_);
    }

    glBindVertexArray(particle_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, particle_instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(cmd.instances.size() * sizeof(ParticleInstance)),
            cmd.instances.data(), GL_STREAM_DRAW);

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(cmd.instances.size()));
    glBindVertexArray(0);
}

void OpenGLRenderBackend::set_ui_painter(ui::IUiPainter* painter) {
    ui_painter_ = painter;
}

void OpenGLRenderBackend::execute(const CommandBuffer& commands) {
    commands.execute(ExecuteVisitor{.painter = ui_painter_, .backend = this});
}

}
