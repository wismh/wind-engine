#include "opengl_mesh.h"

#include <cstddef>

namespace engine::render {

OpenGLMesh::OpenGLMesh(const MeshDesc& desc) {
    vertex_count_ = static_cast<int>(desc.vertices.size());
    if (vertex_count_ <= 0) {
        return;
    }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.vertices.size() * sizeof(MeshVertex)),
            desc.vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), nullptr);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
            reinterpret_cast<const void*>(offsetof(MeshVertex, uv)));

    glBindVertexArray(0);
}

OpenGLMesh::~OpenGLMesh() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
    }
}

void OpenGLMesh::draw() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count_);
    glBindVertexArray(0);
}

}
