#include "opengl_texture.h"

#include "context_scope.h"

#include "render/texture_check.h"

#include <engine/log.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace engine::render {
namespace {

#if defined(ENGINE_WITH_GLES)
constexpr GLint kInternalFormat = GL_RGBA8;
#else
constexpr GLint kInternalFormat = GL_RGBA;
#endif

GLenum gl_mag_filter(FilterMode filter) {
    return filter == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
}

GLenum gl_min_filter(FilterMode filter, MipmapMode mipmaps) {
    const bool nearest = filter == FilterMode::Nearest;
    switch (mipmaps) {
        case MipmapMode::Nearest:
            return nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST;
        case MipmapMode::Linear:
            return nearest ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
        case MipmapMode::None:
        default:
            return gl_mag_filter(filter);
    }
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
    allocate(desc.width, desc.height, flipped.data(), desc.filter, desc.wrap, desc.mipmaps);
}

OpenGLTexture::OpenGLTexture(const TextureArrayDesc& desc) : width_(desc.width), height_(desc.height) {
    if (const std::string error = texture_array_desc_error(desc); !error.empty()) {
        log::error(error);
        return;
    }
    std::vector<std::uint8_t> flipped;
    if (!flip_layers_vertically_into(desc.rgba, desc.width, desc.height, desc.layers, flipped)) {
        return;
    }
    layers_ = desc.layers;
    mipmaps_ = desc.mipmaps;
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, id_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, kInternalFormat, desc.width, desc.height, desc.layers, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, flipped.data());
    apply_sampling(desc.filter, desc.wrap, desc.mipmaps);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

OpenGLTexture::OpenGLTexture(const DynamicTextureDesc& desc, std::function<void()> make_current)
    : width_(desc.width), height_(desc.height), make_current_(std::move(make_current)) {
    if (desc.width <= 0 || desc.height <= 0) {
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4;
    const std::vector<std::uint8_t> blank(bytes, 0);
    allocate(desc.width, desc.height, blank.data(), desc.filter, desc.wrap, MipmapMode::None);
}

unsigned int OpenGLTexture::target() const noexcept {
    return layers_ > 0 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
}

void OpenGLTexture::apply_sampling(FilterMode filter, WrapMode wrap_mode, MipmapMode mipmaps) const {
    const GLenum target_gl = target();
    const GLenum wrap = gl_wrap(wrap_mode);
    glTexParameteri(target_gl, GL_TEXTURE_MIN_FILTER, gl_min_filter(filter, mipmaps));
    glTexParameteri(target_gl, GL_TEXTURE_MAG_FILTER, gl_mag_filter(filter));
    glTexParameteri(target_gl, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(target_gl, GL_TEXTURE_WRAP_T, wrap);
    if (mipmaps != MipmapMode::None) {
        // Core in GL 3.0, GLES 3.0, and WebGL2; per layer for an array.
        glGenerateMipmap(target_gl);
    }
}

void OpenGLTexture::allocate(int width, int height, const std::uint8_t* pixels, FilterMode filter_mode,
        WrapMode wrap_mode, MipmapMode mipmaps) {
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, kInternalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    mipmaps_ = mipmaps;
    apply_sampling(filter_mode, wrap_mode, mipmaps);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool OpenGLTexture::update(std::span<const std::uint8_t> rgba, int width, int height) {
    if (id_ == 0 || layers_ > 0 || !flip_image_vertically_into(rgba, width, height, staging_)) {
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

bool OpenGLTexture::update_rects(
        std::span<const TextureRect> rects, std::span<const std::uint8_t> rgba, int row_stride) {
    if (id_ == 0 || layers_ > 0 || mipmaps_ != MipmapMode::None) {
        return false;
    }
    if (rects.empty()) {
        return true;
    }
    if (const std::string error = texture_rects_error(width_, height_, rects, rgba, row_stride); !error.empty()) {
        log::error(error);
        return false;
    }
    std::size_t max_bytes = 0;
    for (const TextureRect& rect : rects) {
        max_bytes = std::max(
                max_bytes, static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height) * 4u);
    }
    staging_.resize(max_bytes);
    const ContextScope scope(make_current_);
    glBindTexture(GL_TEXTURE_2D, id_);
    for (const TextureRect& rect : rects) {
        const std::size_t bytes = static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height) * 4u;
        if (!pack_texture_rect(rect, rgba, row_stride, std::span<std::uint8_t>(staging_.data(), bytes))) {
            glBindTexture(GL_TEXTURE_2D, 0);
            return false;
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x, texture_rect_gpu_y(height_, rect), rect.width, rect.height, GL_RGBA,
                GL_UNSIGNED_BYTE, staging_.data());
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
    glBindTexture(target(), id_);
}

}
