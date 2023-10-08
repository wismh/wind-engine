#pragma once

#include <engine/render/graphics.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::render {

struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec2 uv{0.0f};
};

struct MeshDesc {
    std::vector<MeshVertex> vertices;
};

struct ShaderDesc {
    std::string vertex_src;
    std::string fragment_src;
};

struct TextureDesc {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

class IGraphicFactory {
public:
    virtual ~IGraphicFactory() = default;
    virtual std::shared_ptr<IMesh> create_mesh(const MeshDesc& desc) = 0;
    virtual std::shared_ptr<IShader> create_shader(const ShaderDesc& desc) = 0;
    virtual std::shared_ptr<ITexture> create_texture(const TextureDesc& desc) = 0;
};

}
