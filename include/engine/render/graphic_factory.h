#pragma once

// docs/tech/modules/Render.md

#include <engine/render/graphics.h>
#include <engine/resources/meta.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine::render {

// Vertex shader inputs: position at location 0, uv at 1, normal at 2, color at 3, layer at 4. A shader ignores what
// it does not read: the 2D shaders read 0 and 1, the builtin unlit and lit shaders also read the color (baked light,
// AO, tint), and the builtin array shaders also read the layer.
struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
    // Texture array layer the builtin array shaders sample (`TextureArray::layer`). Give all three vertices of a
    // triangle the same value: it is not interpolated. Ignored by every other shader.
    float layer = 0.0f;
};

// Four bone influences for one vertex. `joints` indexes the skin's joint list (not the node list). Unused slots are
// joint 0 with weight 0. A mesh with no skin leaves `MeshDesc::influences` empty.
struct SkinInfluence {
    std::uint8_t joints[4]{};
    float weights[4]{0.0f, 0.0f, 0.0f, 0.0f};
};

// A triangle list. Empty `indices`: every three vertices are one triangle. Otherwise every three indices are one, each
// below `vertices.size()`. Front faces wind counter-clockwise. `influences` is empty, or one entry per vertex.
// `bounds_override` replaces the box around the vertex positions (a skinned mesh's posed extent).
struct MeshDesc {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SkinInfluence> influences;
    std::optional<Aabb> bounds_override;
};

// The box around every vertex position of `desc`, indexed or not (a vertex no index uses still counts). A zero box at
// the origin when there are no vertices.
[[nodiscard]] Aabb mesh_bounds(const MeshDesc& desc);

struct ShaderDesc {
    std::string vertex_src;
    std::string fragment_src;
    std::vector<std::uint32_t> vertex_spirv;
    std::vector<std::uint32_t> fragment_spirv;
};

struct TextureDesc {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    FilterMode filter = FilterMode::Linear;
    WrapMode wrap = WrapMode::Clamp;
    MipmapMode mipmaps = MipmapMode::None;
};

// The most layers a texture array may have: the minimum OpenGL 3.3, OpenGL ES 3.0, and Vulkan all guarantee.
inline constexpr int kMaxTextureArrayLayers = 256;

// `layers` images of `width` x `height` in one texture: `rgba` is layer 0 first, each layer top row first (the layout
// of `TextureDesc::rgba`), so it is at least `width * height * 4 * layers` bytes. Mip levels are made per layer.
struct TextureArrayDesc {
    int width = 0;
    int height = 0;
    int layers = 0;
    std::vector<std::uint8_t> rgba;
    FilterMode filter = FilterMode::Linear;
    WrapMode wrap = WrapMode::Clamp;
    MipmapMode mipmaps = MipmapMode::None;
};

// Starts transparent black. `update` fills it. It has no mip levels.
struct DynamicTextureDesc {
    int width = 0;
    int height = 0;
    FilterMode filter = FilterMode::Linear;
    WrapMode wrap = WrapMode::Clamp;
};

class IGraphicFactory {
public:
    virtual ~IGraphicFactory() = default;
    virtual std::shared_ptr<IMesh> create_mesh(const MeshDesc& desc) = 0;
    virtual std::shared_ptr<IShader> create_shader(const ShaderDesc& desc) = 0;
    virtual std::shared_ptr<ITexture> create_texture(const TextureDesc& desc) = 0;
    // An `ITexture` whose `layers()` is `desc.layers`. Null for a size or layer count that is not positive, more than
    // `kMaxTextureArrayLayers` layers, or an `rgba` too short for them (the reason is logged).
    virtual std::shared_ptr<ITexture> create_texture_array(const TextureArrayDesc& desc) = 0;
    // Null for a size that is not positive.
    virtual std::shared_ptr<IDynamicTexture> create_dynamic_texture(const DynamicTextureDesc& desc) = 0;
};

}
