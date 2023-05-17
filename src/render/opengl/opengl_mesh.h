#pragma once

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

    void draw() const;

    [[nodiscard]] bool valid() const noexcept {
        return vao_ != 0;
    }

private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int vertex_count_ = 0;
};

}
