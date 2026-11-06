#include "opengl_mesh.h"

#include "context_scope.h"

#include "render/mesh_check.h"

#include <engine/log.h>
#include <engine/render/commands.h>

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
    : IMesh(desc)
    , make_current_(std::move(make_current)) {
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
    upload(desc);
}

OpenGLMesh::~OpenGLMesh() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
    if (ebo_ != 0) {
        glDeleteBuffers(1, &ebo_);
    }
    if (skin_vbo_ != 0) {
        glDeleteBuffers(1, &skin_vbo_);
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
    } else {
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

    const bool skin = !desc.influences.empty();
    if (skin != has_skin_) {
        // The VAO's attribute enables were recorded without this buffer, or with it.
        vao_.release();
    }
    has_skin_ = skin;
    if (!skin) {
        return;
    }
    if (skin_vbo_ == 0) {
        glGenBuffers(1, &skin_vbo_);
        vao_.release();
    }
    glBindBuffer(GL_ARRAY_BUFFER, skin_vbo_);
    write_buffer(GL_ARRAY_BUFFER, desc.influences.data(), desc.influences.size() * sizeof(SkinInfluence),
            skin_capacity_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLMesh::configure_vertex_array() const {
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

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
            reinterpret_cast<const void*>(offsetof(MeshVertex, color)));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
            reinterpret_cast<const void*>(offsetof(MeshVertex, layer)));

    if (has_skin_ && skin_vbo_ != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, skin_vbo_);
        glEnableVertexAttribArray(5);
        glVertexAttribIPointer(5, 4, GL_UNSIGNED_BYTE, sizeof(SkinInfluence), nullptr);
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(SkinInfluence),
                reinterpret_cast<const void*>(offsetof(SkinInfluence, weights)));
    }

    if (ebo_ != 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    }
}

void OpenGLMesh::draw() {
    // The buffers are shared by every window's context; the VAO is made in each context that draws the mesh.
    const GLuint vao = vao_.current([this] { configure_vertex_array(); });
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

void OpenGLMesh::draw_instanced(unsigned int instance_buffer, int instance_count) {
    if (instance_count <= 0 || instance_buffer == 0) {
        return;
    }
    const GLuint vao = vao_.current([this] { configure_vertex_array(); });
    if (vao == 0) {
        return;
    }
    glBindVertexArray(vao);
    // Any return after the enables still clears them and divisor 0 before the VAO is unbound.
    struct ResetInstanceAttribs {
        ~ResetInstanceAttribs() {
            for (GLuint location = 7; location <= 10; ++location) {
                glDisableVertexAttribArray(location);
                glVertexAttribDivisor(location, 0);
            }
            glBindVertexArray(0);
        }
    } reset;

    glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);
    const auto stride = static_cast<GLsizei>(sizeof(MeshParticleInstance));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(offsetof(MeshParticleInstance, position)));
    glVertexAttribDivisor(7, 1);
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 3, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(offsetof(MeshParticleInstance, scale)));
    glVertexAttribDivisor(8, 1);
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(offsetof(MeshParticleInstance, color)));
    glVertexAttribDivisor(9, 1);
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(10, 1, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(offsetof(MeshParticleInstance, layer)));
    glVertexAttribDivisor(10, 1);

    if (indexed_) {
        glDrawElementsInstanced(GL_TRIANGLES, draw_count_, GL_UNSIGNED_INT, nullptr, instance_count);
    } else {
        glDrawArraysInstanced(GL_TRIANGLES, 0, draw_count_, instance_count);
    }
    (void)reset;
}

}
