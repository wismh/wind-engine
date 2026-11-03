#pragma once

// docs/tech/modules/Render.md#texture-arrays

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

}
