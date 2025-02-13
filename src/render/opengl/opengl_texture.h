#pragma once

#include "gl_includes.h"

#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>

#include <cstdint>
#include <span>
#include <vector>

namespace engine::render {

[[nodiscard]] std::vector<std::uint8_t> flip_image_vertically(
        std::span<const std::uint8_t> rgba, int width, int height);

class OpenGLTexture final : public ITexture {
public:
    explicit OpenGLTexture(const TextureDesc& desc);
    ~OpenGLTexture() override;

    OpenGLTexture(const OpenGLTexture&) = delete;
    OpenGLTexture& operator=(const OpenGLTexture&) = delete;

    void bind(int slot) const;

    [[nodiscard]] bool valid() const noexcept {
        return id_ != 0;
    }

private:
    unsigned int id_ = 0;
};

}
