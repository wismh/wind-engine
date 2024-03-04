#include "opengl_texture.h"

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

OpenGLTexture::OpenGLTexture(const TextureDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0 || desc.rgba.empty()) {
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
            desc.width, desc.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, desc.rgba.data());
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
