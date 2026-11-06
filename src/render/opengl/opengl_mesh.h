#pragma once

#include "context_vao.h"
#include "gl_includes.h"

#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>

#include <cstddef>
#include <functional>

namespace engine::render {

class OpenGLMesh final : public IMesh {
public:
    // An invalid desc (render/mesh_check.h) is logged and leaves the mesh invalid. `make_current` makes the engine's
    // GL context current; `update` runs it, as the factory does for creation.
    OpenGLMesh(const MeshDesc& desc, std::function<void()> make_current);
    ~OpenGLMesh() override;

    OpenGLMesh(const OpenGLMesh&) = delete;
    OpenGLMesh& operator=(const OpenGLMesh&) = delete;

    void draw();
    // Enables locations 7-10 on this mesh's VAO, draws `instance_count` instances from `instance_buffer`, then
    // disables those locations and sets their divisor back to 0 before the VAO is unbound.
    void draw_instanced(unsigned int instance_buffer, int instance_count);

    [[nodiscard]] bool valid() const noexcept {
        return vbo_ != 0;
    }

protected:
    bool replace(const MeshDesc& desc) override;

private:
    // Puts `desc` into the buffers: in place when it fits, otherwise into larger storage of the same buffer names.
    void upload(const MeshDesc& desc);
    void configure_vertex_array() const;

    std::function<void()> make_current_;
    unsigned int vbo_ = 0;
    // 0 until the mesh has had indices. Kept when an update drops them; `indexed_` says whether draw uses it.
    unsigned int ebo_ = 0;
    // Skin influences (locations 5 and 6). 0 when the mesh has never had any.
    unsigned int skin_vbo_ = 0;
    // Bytes of storage behind vbo_ and ebo_. They grow on update and never shrink.
    std::size_t vbo_capacity_ = 0;
    std::size_t ebo_capacity_ = 0;
    std::size_t skin_capacity_ = 0;
    bool indexed_ = false;
    bool has_skin_ = false;
    ContextVao vao_;
    // Vertices drawn by glDrawArrays, or indices by glDrawElements when indexed_.
    int draw_count_ = 0;
};

}
