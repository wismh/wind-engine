#pragma once

// docs/tech/modules/Render.md

#include <engine/render/graphic_factory.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::render {

// Why `desc` cannot become a texture array: a size or layer count that is not positive, more than
// `kMaxTextureArrayLayers` layers, or an `rgba` shorter than `width * height * 4 * layers`. Empty when it can.
[[nodiscard]] std::string texture_array_desc_error(const TextureArrayDesc& desc);

// Mip levels of a full chain for a `width` x `height` image: floor(log2(max(width, height))) + 1, at least 1.
[[nodiscard]] int mip_level_count(int width, int height) noexcept;

// `layers` images of `width` x `height`, each top row first, flipped so each layer's bottom row comes first (the row
// order OpenGL and the Vulkan upload expect, so v = 1 is the top row). False, with `out` untouched, for a size that is
// not positive or an `rgba` shorter than `width * height * 4 * layers`.
[[nodiscard]] bool flip_layers_vertically_into(
        std::span<const std::uint8_t> rgba, int width, int height, int layers, std::vector<std::uint8_t>& out);

// Why `rects` cannot be written into a `width` x `height` texture from `rgba` (`row_stride` pixels per row, top row
// first). Empty when they can, including when `rects` itself is empty.
[[nodiscard]] std::string texture_rects_error(int width, int height, std::span<const TextureRect> rects,
        std::span<const std::uint8_t> rgba, int row_stride);

// GPU row of the bottom of `rect` in a texture of `texture_height` whose row 0 is the bottom row (the upload layout
// of a dynamic texture, so v = 1 stays the top row).
[[nodiscard]] constexpr int texture_rect_gpu_y(int texture_height, const TextureRect& rect) noexcept {
    return texture_height - (rect.y + rect.height);
}

// Copies `rects` from `rgba` (top row first, `row_stride` pixels per row) into `dest`, a `width` x `height` image
// tightly packed with its bottom row first. Pixels outside the rects stay. False, with `dest` untouched, when
// `texture_rects_error` rejects the rects or `dest` is shorter than `width * height * 4`. An empty `rects` leaves
// `dest` untouched and returns true.
[[nodiscard]] bool write_texture_rects(int width, int height, std::span<const TextureRect> rects,
        std::span<const std::uint8_t> rgba, int row_stride, std::span<std::uint8_t> dest);

// Packs one rect tightly, bottom row first, into `dest` (`rect.width * rect.height * 4` bytes). False, with `dest`
// untouched, when the rect is empty or `dest` or `rgba` cannot hold it.
[[nodiscard]] bool pack_texture_rect(const TextureRect& rect, std::span<const std::uint8_t> rgba, int row_stride,
        std::span<std::uint8_t> dest);

}
