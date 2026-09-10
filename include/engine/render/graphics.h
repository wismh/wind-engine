#pragma once

// docs/tech/modules/Render.md

#include <cstdint>
#include <span>

namespace engine::render {

class IMesh {
public:
    virtual ~IMesh() = default;
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
