#pragma once

#include "context_vao.h"
#include "gl_includes.h"

#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>

namespace engine::render {

class OpenGLMesh final : public IMesh {
public:
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
    ContextVao vao_;
    int vertex_count_ = 0;
};

}
