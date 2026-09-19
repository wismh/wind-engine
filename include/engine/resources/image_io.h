#pragma once

// docs/tech/features/Assets.md

#include <engine/core/export.h>
#include <engine/render/graphic_factory.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace engine {

// RGBA8 of a PNG file's bytes, top row first. Nullopt when the bytes are not a PNG the decoder reads.
// Pure CPU work: no GPU, no asset database, so a worker thread may call it.
[[nodiscard]] ENGINE_API std::optional<render::TextureDesc> decode_png_rgba(std::string_view bytes);
// In-memory PNG of an RGBA8 image, top row first. Empty on encode failure.
[[nodiscard]] ENGINE_API std::vector<std::uint8_t> encode_png_rgba(const render::TextureDesc& image);
// `image` scaled down, keeping its aspect, so its longer side is at most `max_edge` (linear filter). An image that
// already fits is returned as it is. An empty or short image, or `max_edge` below 1, gives an empty one.
[[nodiscard]] ENGINE_API render::TextureDesc fit_within(const render::TextureDesc& image, int max_edge);

}
