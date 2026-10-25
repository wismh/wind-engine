#pragma once

// docs/tech/modules/Capture.md

#include <cstdint>
#include <vector>

struct SDL_Surface;

namespace engine::capture {

// Converts any SDL surface (RGB, packed or planar YUV) to RGBA8 sRGB, top row first, into `out` (resized, its
// storage reused). False for a null or empty surface or a failed conversion. Window builds only.
[[nodiscard]] bool convert_surface_to_rgba(SDL_Surface* surface, std::vector<std::uint8_t>& out);

}
