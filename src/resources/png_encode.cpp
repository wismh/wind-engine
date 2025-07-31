#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4456)
#pragma warning(disable : 4505)
#pragma warning(disable : 4996)
#endif

#include "stb_image_write.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "importers.h"

#include <cstddef>
#include <cstdlib>

namespace engine {

std::vector<std::uint8_t> encode_png_rgba(const render::TextureDesc& image) {
    if (image.width <= 0 || image.height <= 0 ||
            image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4u) {
        return {};
    }
    int out_len = 0;
    unsigned char* png = stbi_write_png_to_mem(image.rgba.data(), 0, image.width, image.height, 4, &out_len);
    if (png == nullptr || out_len <= 0) {
        return {};
    }
    std::vector<std::uint8_t> out(png, png + out_len);
    std::free(png);
    return out;
}

}
