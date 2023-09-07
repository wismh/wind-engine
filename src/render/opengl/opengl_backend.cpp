#include "opengl_backend.h"

#include "gl_includes.h"
#include "opengl_mesh.h"
#include "opengl_shader.h"
#include "opengl_texture.h"

#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/render/material.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <memory>

namespace engine::render {
namespace {

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

    apply_blend(cmd.material->Blend());

    const auto shader = std::dynamic_pointer_cast<OpenGLShader>(cmd.material->Shader());
    const auto mesh = std::dynamic_pointer_cast<OpenGLMesh>(cmd.mesh);
    if (!shader || !mesh || !shader->valid() || !mesh->valid()) {
        return;
    }

    shader->use();
    shader->set_mat4("uModel", cmd.model);
    shader->set_mat4("uView", cmd.view);
    shader->set_mat4("uProjection", cmd.projection);
    shader->set_vec4("uColor", cmd.material->Color() * cmd.color);
    shader->set_int("uTexture", 0);

    if (const auto texture = std::dynamic_pointer_cast<OpenGLTexture>(cmd.material->Texture(0))) {
        if (texture->valid()) {
            texture->bind(0);
        }
    }

    mesh->draw();
}

struct ExecuteVisitor {
    ui::IUiPainter* painter = nullptr;

    void operator()(const CmdDrawMesh& cmd) const {
        execute_draw_mesh(cmd);
    }

    void operator()(const CmdDrawUI& cmd) const {
        if (painter == nullptr || cmd.document == nullptr) {
            return;
        }
        ui::UiDocument document = *cmd.document;
        ui::paint_document(document, cmd.stylesheet, *painter,
                ui::UiPaintInput{.canvas_rect = cmd.rect, .pointer = cmd.pointer, .pointer_down = cmd.pointer_down});
    }
};

}

void OpenGLRenderBackend::set_ui_painter(ui::IUiPainter* painter) {
    ui_painter_ = painter;
}

void OpenGLRenderBackend::execute(const CommandBuffer& commands) {
    commands.execute(ExecuteVisitor{.painter = ui_painter_});
}

}
