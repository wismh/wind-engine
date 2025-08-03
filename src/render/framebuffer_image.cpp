#include "render/framebuffer_image.h"

#include <cstddef>

namespace engine::render {

TextureDesc framebuffer_image(std::span<const std::uint8_t> bottom_up, int width, int height, bool transparent) {
    TextureDesc image;
    if (width <= 0 || height <= 0) {
        return image;
    }
    const std::size_t row = static_cast<std::size_t>(width) * 4u;
    if (bottom_up.size() != row * static_cast<std::size_t>(height)) {
        return image;
    }
    image.width = width;
    image.height = height;
    image.rgba.resize(bottom_up.size());
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* source = bottom_up.data() + row * static_cast<std::size_t>(height - 1 - y);
        std::uint8_t* target = image.rgba.data() + row * static_cast<std::size_t>(y);
        for (std::size_t i = 0; i < row; i += 4) {
            const std::uint8_t alpha = source[i + 3];
            if (!transparent) {
                target[i] = source[i];
                target[i + 1] = source[i + 1];
                target[i + 2] = source[i + 2];
                target[i + 3] = 255;
                continue;
            }
            for (std::size_t c = 0; c < 3; ++c) {
                const unsigned value = alpha == 0 ? 0u : (source[i + c] * 255u + alpha / 2u) / alpha;
                target[i + c] = static_cast<std::uint8_t>(value > 255u ? 255u : value);
            }
            target[i + 3] = alpha;
        }
    }
    return image;
}

}
