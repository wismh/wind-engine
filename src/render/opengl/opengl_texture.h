#pragma once

#include "gl_includes.h"

#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace engine::render {

[[nodiscard]] std::vector<std::uint8_t> flip_image_vertically(
        std::span<const std::uint8_t> rgba, int width, int height);

// Same as `flip_image_vertically`, into `out` (resized, its storage reused). False, with `out` untouched, for a
// size that is not positive or an `rgba` shorter than `width * height * 4`.
[[nodiscard]] bool flip_image_vertically_into(
        std::span<const std::uint8_t> rgba, int width, int height, std::vector<std::uint8_t>& out);

// A 2D texture (`GL_TEXTURE_2D`) or, made from a `TextureArrayDesc`, a texture array (`GL_TEXTURE_2D_ARRAY`).
class OpenGLTexture final : public IDynamicTexture {
public:
    explicit OpenGLTexture(const TextureDesc& desc);
    // A texture array. Stays invalid, with no GL call, for a desc `texture_array_desc_error` rejects (logged).
    explicit OpenGLTexture(const TextureArrayDesc& desc);
    // `make_current` makes the engine's GL context current; `update` runs it, as the factory does for creation.
    OpenGLTexture(const DynamicTextureDesc& desc, std::function<void()> make_current);
    ~OpenGLTexture() override;

    OpenGLTexture(const OpenGLTexture&) = delete;
    OpenGLTexture& operator=(const OpenGLTexture&) = delete;

    // Binds to this texture's target (`target()`) on unit `slot`.
    void bind(int slot) const;

    // False for a texture array, which is never dynamic.
    bool update(std::span<const std::uint8_t> rgba, int width, int height) override;

    [[nodiscard]] int layers() const noexcept override {
        return layers_;
    }

    // `GL_TEXTURE_2D_ARRAY` for a texture array, `GL_TEXTURE_2D` otherwise.
    [[nodiscard]] unsigned int target() const noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return id_ != 0;
    }

    [[nodiscard]] int width() const noexcept override {
        return width_;
    }

    [[nodiscard]] int height() const noexcept override {
        return height_;
    }

private:
    void allocate(int width, int height, const std::uint8_t* pixels, FilterMode filter, WrapMode wrap,
            MipmapMode mipmaps);
    // Sampler state of the bound texture on `target()`, and its mip chain when `mipmaps` is not None.
    void apply_sampling(FilterMode filter, WrapMode wrap, MipmapMode mipmaps) const;

    unsigned int id_ = 0;
    int width_ = 0;
    int height_ = 0;
    int layers_ = 0;
    std::function<void()> make_current_;
    // Reused by every `update`.
    std::vector<std::uint8_t> staging_;
};

}
