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

class OpenGLTexture final : public IDynamicTexture {
public:
    explicit OpenGLTexture(const TextureDesc& desc);
    // `make_current` makes the engine's GL context current; `update` runs it, as the factory does for creation.
    OpenGLTexture(const DynamicTextureDesc& desc, std::function<void()> make_current);
    ~OpenGLTexture() override;

    OpenGLTexture(const OpenGLTexture&) = delete;
    OpenGLTexture& operator=(const OpenGLTexture&) = delete;

    void bind(int slot) const;

    bool update(std::span<const std::uint8_t> rgba, int width, int height) override;

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
    void allocate(int width, int height, const std::uint8_t* pixels, FilterMode filter, WrapMode wrap);

    unsigned int id_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::function<void()> make_current_;
    // Reused by every `update`.
    std::vector<std::uint8_t> staging_;
};

}
