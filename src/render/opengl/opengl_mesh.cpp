#include "opengl_mesh.h"

#include <cstddef>

namespace engine::render {

OpenGLMesh::OpenGLMesh(const MeshDesc& desc) {
    vertex_count_ = static_cast<int>(desc.vertices.size());
    if (vertex_count_ <= 0) {
        return;
    }

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.vertices.size() * sizeof(MeshVertex)),
            desc.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

OpenGLMesh::~OpenGLMesh() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
}

void OpenGLMesh::draw() {
    // The buffer is shared by every window's context; the VAO is made in each context that draws the mesh.
    const GLuint vao = vao_.current([this] {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), nullptr);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                reinterpret_cast<const void*>(offsetof(MeshVertex, uv)));
    });
    if (vao == 0) {
        return;
    }
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count_);
    glBindVertexArray(0);
}

}
