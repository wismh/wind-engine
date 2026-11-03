#include "opengl_mesh.h"

#include "context_scope.h"

#include "render/mesh_check.h"

#include <engine/log.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace engine::render {

namespace {

// Storage for `needed` bytes in a buffer that has `capacity`: grow by half again so a mesh that gains a little at each
// edit does not reallocate every time.
std::size_t grown_capacity(std::size_t needed, std::size_t capacity) {
    return std::max(needed, capacity + capacity / 2);
}

// Writes `bytes` of `data` into the buffer bound to `target`: in place when they fit in `capacity`, otherwise into new
// storage of the same name (the binding a VAO holds stays valid).
void write_buffer(GLenum target, const void* data, std::size_t bytes, std::size_t& capacity) {
    if (bytes <= capacity) {
        glBufferSubData(target, 0, static_cast<GLsizeiptr>(bytes), data);
        return;
    }
    capacity = grown_capacity(bytes, capacity);
    glBufferData(target, static_cast<GLsizeiptr>(capacity), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(target, 0, static_cast<GLsizeiptr>(bytes), data);
}

}

OpenGLMesh::OpenGLMesh(const MeshDesc& desc, std::function<void()> make_current)
    : make_current_(std::move(make_current)) {
    if (const std::string error = mesh_desc_error(desc); !error.empty()) {
        log::error(error);
        return;
    }

    const std::size_t vertex_bytes = desc.vertices.size() * sizeof(MeshVertex);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_bytes), desc.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vbo_capacity_ = vertex_bytes;

    if (desc.indices.empty()) {
        draw_count_ = static_cast<int>(desc.vertices.size());
        return;
    }

    // The element binding is VAO state: upload with no VAO bound so another object's VAO does not pick it up. Each
    // context's VAO binds the buffer in draw.
    const std::size_t index_bytes = desc.indices.size() * sizeof(std::uint32_t);
    GLint bound_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &bound_vao);
    glBindVertexArray(0);
    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_bytes), desc.indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(static_cast<GLuint>(bound_vao));
    ebo_capacity_ = index_bytes;
    indexed_ = true;
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

bool OpenGLMesh::replace(const MeshDesc& desc) {
    if (!valid()) {
        return false;
    }
    const ContextScope scope(make_current_);
    upload(desc);
    return true;
}

void OpenGLMesh::upload(const MeshDesc& desc) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    write_buffer(GL_ARRAY_BUFFER, desc.vertices.data(), desc.vertices.size() * sizeof(MeshVertex), vbo_capacity_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    indexed_ = !desc.indices.empty();
    if (!indexed_) {
        draw_count_ = static_cast<int>(desc.vertices.size());
        return;
    }

    GLint bound_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &bound_vao);
    glBindVertexArray(0);
    if (ebo_ == 0) {
        glGenBuffers(1, &ebo_);
        // The VAOs made so far bind no element buffer: let each context make a new one on its next draw.
        vao_.release();
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    write_buffer(GL_ELEMENT_ARRAY_BUFFER, desc.indices.data(), desc.indices.size() * sizeof(std::uint32_t),
            ebo_capacity_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(static_cast<GLuint>(bound_vao));
    draw_count_ = static_cast<int>(desc.indices.size());
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
    if (indexed_) {
        glDrawElements(GL_TRIANGLES, draw_count_, GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, draw_count_);
    }
    glBindVertexArray(0);
}

}
