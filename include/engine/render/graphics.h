#pragma once

// docs/tech/modules/Render.md

#include <engine/render/aabb.h>

#include <cstdint>
#include <span>

namespace engine::render {

struct MeshDesc;

// A triangle mesh on the GPU, made by `IGraphicFactory::create_mesh`. Main thread only.
class IMesh {
public:
    virtual ~IMesh() = default;

    // Replaces every vertex and index with those of `desc`, as if the mesh were made from it by `create_mesh`, and
    // `bounds()` with `mesh_bounds(desc)`. False, and the mesh keeps what it had (bounds included), when `desc` cannot
    // be drawn (no vertices, an index count that is not a multiple of 3, or an index at or past the vertex count; the
    // reason is logged) or the backend could not take it.
    bool update(const MeshDesc& desc);

    // The local box around every vertex of the desc the mesh was made or last updated from. Frustum culling tests it
    // under the draw's model matrix.
    [[nodiscard]] const Aabb& bounds() const noexcept {
        return bounds_;
    }

protected:
    // Every mesh starts from a desc; its bounds are `mesh_bounds(desc)`.
    explicit IMesh(const MeshDesc& desc);

    // The backend half of `update`, called only with a desc that can be drawn. False leaves the old contents.
    virtual bool replace(const MeshDesc& desc) = 0;

private:
    Aabb bounds_;
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

// A rectangle of pixels in a texture. `x` and `y` are the top-left in the same space as `TextureDesc::rgba`:
// y = 0 is the top row.
struct TextureRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// A texture whose pixels the game replaces while it runs: a camera frame, a software-rendered image. Created by
// `IGraphicFactory::create_dynamic_texture`. Main thread only.
class IDynamicTexture : public ITexture {
public:
    // Replaces every pixel with `rgba`, 4 bytes per pixel, top row first (the layout of `TextureDesc::rgba`),
    // exactly like a texture made from that data: v = 1 is the top row. A size other than the current one
    // reallocates the texture, and `width()` / `height()` follow. False, and nothing changes, when `width` or
    // `height` is not positive, `rgba` is shorter than `width * height * 4`, the texture is invalid, or the GPU refused.
    virtual bool update(std::span<const std::uint8_t> rgba, int width, int height) = 0;

    // Replaces each rectangle with the pixels at that place in `rgba`. `rgba` is top row first and `row_stride` is
    // its width in pixels, which may be wider than the texture. The texture's size does not change, and pixels
    // outside the rectangles stay. An empty `rects` changes nothing and returns true. False, and nothing changes,
    // when the texture is invalid, it is a texture array or has mipmaps, a rectangle is empty or outside the texture,
    // `row_stride` does not cover a rectangle, `rgba` ends before the last byte a rectangle reads, or the GPU refused. A
    // rejected rectangle is logged.
    virtual bool update_rects(std::span<const TextureRect> rects, std::span<const std::uint8_t> rgba,
            int row_stride) = 0;
};

}
