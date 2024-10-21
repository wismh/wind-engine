#include "opengl_texture.h"

#include <cstring>

namespace engine::render {
namespace {

GLenum gl_min_mag_filter(FilterMode filter) {
    return filter == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
}

GLenum gl_wrap(WrapMode wrap) {
    switch (wrap) {
        case WrapMode::Repeat:
            return GL_REPEAT;
        case WrapMode::Mirror:
            return GL_MIRRORED_REPEAT;
        case WrapMode::Clamp:
        default:
            return GL_CLAMP_TO_EDGE;
    }
}

}

std::vector<std::uint8_t> flip_image_vertically(
        std::span<const std::uint8_t> rgba, int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    const std::size_t required =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (rgba.size() < required) {
        return {};
    }
    std::vector<std::uint8_t> flipped(required);
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        const std::size_t src_offset = static_cast<std::size_t>(y) * row_bytes;
        const std::size_t dst_offset = static_cast<std::size_t>(height - 1 - y) * row_bytes;
        std::memcpy(flipped.data() + dst_offset, rgba.data() + src_offset, row_bytes);
    }
    return flipped;
}

OpenGLTexture::OpenGLTexture(const TextureDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0 || desc.rgba.empty()) {
        return;
    }

    const auto flipped = flip_image_vertically(desc.rgba, desc.width, desc.height);
    if (flipped.empty()) {
        return;
    }

    const GLenum filter = gl_min_mag_filter(desc.filter);
    const GLenum wrap = gl_wrap(desc.wrap);

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0,
#if defined(ENGINE_WITH_GLES)
            GL_RGBA8,
#else
            GL_RGBA,
#endif
            desc.width, desc.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glBindTexture(GL_TEXTURE_2D, 0);
}

OpenGLTexture::~OpenGLTexture() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
    }
}

void OpenGLTexture::bind(int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, id_);
}

}
