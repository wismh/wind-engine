#pragma once

// docs/tech/modules/Render.md

#include <cstdint>
#include <span>

namespace engine::render {

struct MeshDesc;

// A triangle mesh on the GPU, made by `IGraphicFactory::create_mesh`. Main thread only.
class IMesh {
public:
    virtual ~IMesh() = default;

    // Replaces every vertex and index with those of `desc`, as if the mesh were made from it by `create_mesh`.
    // False, and the mesh keeps what it had, when `desc` cannot be drawn (no vertices, an index count that is not a
    // multiple of 3, or an index at or past the vertex count; the reason is logged) or the backend could not take it.
    bool update(const MeshDesc& desc);

protected:
    // The backend half of `update`, called only with a desc that can be drawn. False leaves the old contents.
    virtual bool replace(const MeshDesc& desc) = 0;
};

class IShader {
public:
    virtual ~IShader() = default;
};

class ITexture {
public:
    virtual ~ITexture() = default;
    [[nodiscard]] virtual int width() const noexcept { return 0; }
    [[nodiscard]] virtual int height() const noexcept { return 0; }
    // 0 for a 2D texture; the layer count of a texture array (`IGraphicFactory::create_texture_array`), whose
    // `width()` and `height()` are one layer's. A shader samples a 2D texture through `sampler2D` and an array through
    // `sampler2DArray`; a draw whose albedo is the other kind samples white.
    [[nodiscard]] virtual int layers() const noexcept { return 0; }
};

// A texture whose pixels the game replaces while it runs: a camera frame, a software-rendered image. Created by
// `IGraphicFactory::create_dynamic_texture`. Main thread only.
class IDynamicTexture : public ITexture {
public:
    // Replaces every pixel with `rgba`, 4 bytes per pixel, top row first (the layout of `TextureDesc::rgba`),
    // exactly like a texture made from that data: v = 1 is the top row. A size other than the current one
    // reallocates the texture, and `width()` / `height()` follow. False, and nothing changes, when `width` or
    // `height` is not positive, `rgba` is shorter than `width * height * 4`, or the texture is invalid.
    virtual bool update(std::span<const std::uint8_t> rgba, int width, int height) = 0;
};

}
