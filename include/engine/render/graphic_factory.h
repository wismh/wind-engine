#pragma once

// docs/tech/modules/Render.md

#include <engine/render/graphics.h>
#include <engine/resources/meta.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::render {

// Vertex shader inputs: position at location 0, uv at 1, normal at 2. A shader that reads only 0 and 1 (unlit, every
// 2D shader) ignores the normal.
struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec2 uv{0.0f};
};

// A triangle list. Empty `indices`: every three vertices are one triangle. Otherwise every three indices are one, each
// below `vertices.size()`. Front faces wind counter-clockwise.
struct MeshDesc {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct ShaderDesc {
    std::string vertex_src;
    std::string fragment_src;
};

struct TextureDesc {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    FilterMode filter = FilterMode::Linear;
    WrapMode wrap = WrapMode::Clamp;
};

class IGraphicFactory {
public:
    virtual ~IGraphicFactory() = default;
    virtual std::shared_ptr<IMesh> create_mesh(const MeshDesc& desc) = 0;
    virtual std::shared_ptr<IShader> create_shader(const ShaderDesc& desc) = 0;
    virtual std::shared_ptr<ITexture> create_texture(const TextureDesc& desc) = 0;
};

}
