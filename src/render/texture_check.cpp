#include "texture_check.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>

namespace engine::render {

std::string texture_array_desc_error(const TextureArrayDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0) {
        return std::format("the texture array's layer size {} x {} is not positive", desc.width, desc.height);
    }
    if (desc.layers <= 0) {
        return std::format("the texture array has {} layers", desc.layers);
    }
    if (desc.layers > kMaxTextureArrayLayers) {
        return std::format("the texture array has {} layers, more than {}", desc.layers, kMaxTextureArrayLayers);
    }
    const std::size_t needed = static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4u *
            static_cast<std::size_t>(desc.layers);
    if (desc.rgba.size() < needed) {
        return std::format("the texture array has {} bytes of pixels, {} needed", desc.rgba.size(), needed);
    }
    return {};
}

int mip_level_count(int width, int height) noexcept {
    int size = std::max(width, height);
    int levels = 1;
    while (size > 1) {
        size /= 2;
        ++levels;
    }
    return levels;
}

bool flip_layers_vertically_into(
        std::span<const std::uint8_t> rgba, int width, int height, int layers, std::vector<std::uint8_t>& out) {
    if (width <= 0 || height <= 0 || layers <= 0) {
        return false;
    }
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
    const std::size_t layer_bytes = row_bytes * static_cast<std::size_t>(height);
    const std::size_t required = layer_bytes * static_cast<std::size_t>(layers);
    if (rgba.size() < required) {
        return false;
    }
    out.resize(required);
    for (int layer = 0; layer < layers; ++layer) {
        const std::size_t base = layer_bytes * static_cast<std::size_t>(layer);
        for (int y = 0; y < height; ++y) {
            const std::size_t src = base + static_cast<std::size_t>(y) * row_bytes;
            const std::size_t dst = base + static_cast<std::size_t>(height - 1 - y) * row_bytes;
            std::memcpy(out.data() + dst, rgba.data() + src, row_bytes);
        }
    }
    return true;
}

}
