#include "opengl_mesh.h"

#include "render/mesh_check.h"

#include <engine/log.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace engine::render {

OpenGLMesh::OpenGLMesh(const MeshDesc& desc) {
    if (const std::string error = mesh_desc_error(desc); !error.empty()) {
        log::error(error);
        return;
    }

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.vertices.size() * sizeof(MeshVertex)),
            desc.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (desc.indices.empty()) {
        draw_count_ = static_cast<int>(desc.vertices.size());
        return;
    }

    // The element binding is VAO state: upload with no VAO bound so another object's VAO does not pick it up. Each
    // context's VAO binds the buffer in draw.
    GLint bound_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &bound_vao);
    glBindVertexArray(0);
    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.indices.size() * sizeof(std::uint32_t)),
            desc.indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(static_cast<GLuint>(bound_vao));
    draw_count_ = static_cast<int>(desc.indices.size());
}

OpenGLMesh::~OpenGLMesh() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
    if (ebo_ != 0) {
        glDeleteBuffers(1, &ebo_);
    }
}

void OpenGLMesh::draw() {
    // The buffers are shared by every window's context; the VAO is made in each context that draws the mesh.
    const GLuint vao = vao_.current([this] {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                reinterpret_cast<const void*>(offsetof(MeshVertex, position)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                reinterpret_cast<const void*>(offsetof(MeshVertex, uv)));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                reinterpret_cast<const void*>(offsetof(MeshVertex, normal)));

        if (ebo_ != 0) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        }
    });
    if (vao == 0) {
        return;
    }
    glBindVertexArray(vao);
    if (ebo_ != 0) {
        glDrawElements(GL_TRIANGLES, draw_count_, GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, draw_count_);
    }
    glBindVertexArray(0);
}

}
