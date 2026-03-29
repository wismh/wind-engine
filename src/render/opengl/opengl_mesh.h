#pragma once

#include "context_vao.h"
#include "gl_includes.h"

#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>

namespace engine::render {

class OpenGLMesh final : public IMesh {
public:
    // An invalid desc (render/mesh_check.h) is logged and leaves the mesh invalid.
    explicit OpenGLMesh(const MeshDesc& desc);
    ~OpenGLMesh() override;

    OpenGLMesh(const OpenGLMesh&) = delete;
    OpenGLMesh& operator=(const OpenGLMesh&) = delete;

    void draw();

    [[nodiscard]] bool valid() const noexcept {
        return vbo_ != 0;
    }

private:
    unsigned int vbo_ = 0;
    // 0 for a non-indexed mesh.
    unsigned int ebo_ = 0;
    ContextVao vao_;
    // Vertices drawn by glDrawArrays, or indices by glDrawElements when ebo_ is set.
    int draw_count_ = 0;
};

}
