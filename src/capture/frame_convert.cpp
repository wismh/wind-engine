#include "frame_convert.h"

#if defined(ENGINE_WITH_WINDOW)

#include <SDL3/SDL_surface.h>

#include <cstddef>

namespace engine::capture {

bool convert_surface_to_rgba(SDL_Surface* surface, std::vector<std::uint8_t>& out) {
    if (surface == nullptr || surface->w <= 0 || surface->h <= 0 || surface->pixels == nullptr) {
        return false;
    }
    const int dst_pitch = surface->w * 4;
    out.resize(static_cast<std::size_t>(dst_pitch) * static_cast<std::size_t>(surface->h));
    return SDL_ConvertPixelsAndColorspace(
            surface->w, surface->h, surface->format, SDL_GetSurfaceColorspace(surface), 0, surface->pixels,
            surface->pitch, SDL_PIXELFORMAT_RGBA32, SDL_COLORSPACE_SRGB, 0, out.data(), dst_pitch);
}

}

#endif
