#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4456)
#pragma warning(disable : 4505)
#endif

#include "stb_image.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "importers.h"

#include <cstddef>
#include <limits>

namespace engine {

std::optional<render::TextureDesc> decode_png_rgba(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* const pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
            static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return std::nullopt;
    }

    render::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    const std::size_t count =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    desc.rgba.assign(pixels, pixels + count);
    stbi_image_free(pixels);
    return desc;
}

}
