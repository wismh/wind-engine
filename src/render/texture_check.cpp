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

std::string texture_rects_error(int width, int height, std::span<const TextureRect> rects,
        std::span<const std::uint8_t> rgba, int row_stride) {
    if (rects.empty()) {
        return {};
    }
    if (width <= 0 || height <= 0) {
        return std::format("the texture size {} x {} is not positive", width, height);
    }
    if (row_stride <= 0) {
        return std::format("the texture row stride {} is not positive", row_stride);
    }
    std::size_t needed = 0;
    for (const TextureRect& rect : rects) {
        if (rect.width <= 0 || rect.height <= 0) {
            return std::format("texture rect ({}, {}, {}, {}) is empty", rect.x, rect.y, rect.width, rect.height);
        }
        if (rect.x < 0 || rect.y < 0 || rect.x >= width || rect.y >= height || rect.width > width - rect.x ||
                rect.height > height - rect.y) {
            return std::format("texture rect ({}, {}, {}, {}) is outside the {} x {} texture", rect.x, rect.y,
                    rect.width, rect.height, width, height);
        }
        if (rect.x >= row_stride || rect.width > row_stride - rect.x) {
            return std::format("texture row stride {} does not cover rect ({}, {}, {}, {})", row_stride, rect.x,
                    rect.y, rect.width, rect.height);
        }
        const std::size_t stride_bytes = static_cast<std::size_t>(row_stride) * 4u;
        const std::size_t rect_bytes = static_cast<std::size_t>(rect.y + rect.height - 1) * stride_bytes +
                static_cast<std::size_t>(rect.x + rect.width) * 4u;
        needed = std::max(needed, rect_bytes);
    }
    if (rgba.size() < needed) {
        return std::format("texture rects need {} bytes, {} given", needed, rgba.size());
    }
    return {};
}

bool write_texture_rects(int width, int height, std::span<const TextureRect> rects,
        std::span<const std::uint8_t> rgba, int row_stride, std::span<std::uint8_t> dest) {
    if (rects.empty()) {
        return true;
    }
    if (!texture_rects_error(width, height, rects, rgba, row_stride).empty()) {
        return false;
    }
    const std::size_t image_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (dest.size() < image_bytes) {
        return false;
    }
    const std::size_t src_stride = static_cast<std::size_t>(row_stride) * 4u;
    const std::size_t dst_stride = static_cast<std::size_t>(width) * 4u;
    for (const TextureRect& rect : rects) {
        const std::size_t row_bytes = static_cast<std::size_t>(rect.width) * 4u;
        for (int row = 0; row < rect.height; ++row) {
            const int src_y = rect.y + row;
            const int dst_y = height - 1 - src_y;
            const std::size_t src =
                    static_cast<std::size_t>(src_y) * src_stride + static_cast<std::size_t>(rect.x) * 4u;
            const std::size_t dst =
                    static_cast<std::size_t>(dst_y) * dst_stride + static_cast<std::size_t>(rect.x) * 4u;
            std::memcpy(dest.data() + dst, rgba.data() + src, row_bytes);
        }
    }
    return true;
}

bool pack_texture_rect(const TextureRect& rect, std::span<const std::uint8_t> rgba, int row_stride,
        std::span<std::uint8_t> dest) {
    if (rect.width <= 0 || rect.height <= 0 || row_stride <= 0 || rect.x < 0 || rect.y < 0 ||
            rect.x >= row_stride || rect.width > row_stride - rect.x) {
        return false;
    }
    const std::size_t row_bytes = static_cast<std::size_t>(rect.width) * 4u;
    const std::size_t packed_bytes = row_bytes * static_cast<std::size_t>(rect.height);
    if (dest.size() < packed_bytes) {
        return false;
    }
    const std::size_t src_stride = static_cast<std::size_t>(row_stride) * 4u;
    const std::size_t required = static_cast<std::size_t>(rect.y + rect.height - 1) * src_stride +
            static_cast<std::size_t>(rect.x + rect.width) * 4u;
    if (rgba.size() < required) {
        return false;
    }
    for (int row = 0; row < rect.height; ++row) {
        const int src_y = rect.y + rect.height - 1 - row;
        const std::size_t src = static_cast<std::size_t>(src_y) * src_stride + static_cast<std::size_t>(rect.x) * 4u;
        std::memcpy(dest.data() + static_cast<std::size_t>(row) * row_bytes, rgba.data() + src, row_bytes);
    }
    return true;
}

}
