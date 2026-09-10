#include "opengl_texture.h"

#include "context_scope.h"

#include <cstring>
#include <utility>

namespace engine::render {
namespace {

#if defined(ENGINE_WITH_GLES)
constexpr GLint kInternalFormat = GL_RGBA8;
#else
constexpr GLint kInternalFormat = GL_RGBA;
#endif

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

bool flip_image_vertically_into(
        std::span<const std::uint8_t> rgba, int width, int height, std::vector<std::uint8_t>& out) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t required =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (rgba.size() < required) {
        return false;
    }
    out.resize(required);
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        const std::size_t src_offset = static_cast<std::size_t>(y) * row_bytes;
        const std::size_t dst_offset = static_cast<std::size_t>(height - 1 - y) * row_bytes;
        std::memcpy(out.data() + dst_offset, rgba.data() + src_offset, row_bytes);
    }
    return true;
}

std::vector<std::uint8_t> flip_image_vertically(
        std::span<const std::uint8_t> rgba, int width, int height) {
    std::vector<std::uint8_t> flipped;
    if (!flip_image_vertically_into(rgba, width, height, flipped)) {
        return {};
    }
    return flipped;
}

OpenGLTexture::OpenGLTexture(const TextureDesc& desc) : width_(desc.width), height_(desc.height) {
    if (desc.width <= 0 || desc.height <= 0 || desc.rgba.empty()) {
        return;
    }

    const auto flipped = flip_image_vertically(desc.rgba, desc.width, desc.height);
    if (flipped.empty()) {
        return;
    }
    allocate(desc.width, desc.height, flipped.data(), desc.filter, desc.wrap);
}

OpenGLTexture::OpenGLTexture(const DynamicTextureDesc& desc, std::function<void()> make_current)
    : width_(desc.width), height_(desc.height), make_current_(std::move(make_current)) {
    if (desc.width <= 0 || desc.height <= 0) {
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4;
    const std::vector<std::uint8_t> blank(bytes, 0);
    allocate(desc.width, desc.height, blank.data(), desc.filter, desc.wrap);
}

void OpenGLTexture::allocate(
        int width, int height, const std::uint8_t* pixels, FilterMode filter_mode, WrapMode wrap_mode) {
    const GLenum filter = gl_min_mag_filter(filter_mode);
    const GLenum wrap = gl_wrap(wrap_mode);

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, kInternalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool OpenGLTexture::update(std::span<const std::uint8_t> rgba, int width, int height) {
    if (id_ == 0 || !flip_image_vertically_into(rgba, width, height, staging_)) {
        return false;
    }
    const ContextScope scope(make_current_);
    glBindTexture(GL_TEXTURE_2D, id_);
    if (width == width_ && height == height_) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, staging_.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, kInternalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, staging_.data());
        width_ = width;
        height_ = height;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
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
