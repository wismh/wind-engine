#pragma once

#include <engine/render/graphic_factory.h>

#include <cstdint>
#include <span>

namespace engine::render {

// RGBA8 rows as glReadPixels returns them, bottom row first, as an image with the top row first. An opaque window's
// alpha becomes 255, which is what the screen shows. A transparent window keeps its alpha and is un-premultiplied:
// NanoVG blends premultiplied colors into a clear of zero alpha, and PNG stores straight alpha. Empty when the size
// is not positive or `bottom_up` is not width * height * 4 bytes.
[[nodiscard]] TextureDesc framebuffer_image(std::span<const std::uint8_t> bottom_up, int width, int height,
        bool transparent);

}
