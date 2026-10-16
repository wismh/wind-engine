#include "opengl_ui_device.h"

#include "opengl_shader.h"
#include "opengl_ui_shader.h"

#include "ui/render/ui_pipeline.h"

#include <engine/render/shader_adapt.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace engine::render {
namespace {

#if defined(ENGINE_WITH_GLES)
constexpr ShaderTarget kShaderTarget = ShaderTarget::Glsl300Es;
#else
constexpr ShaderTarget kShaderTarget = ShaderTarget::Glsl330Core;
#endif

GLenum blend_factor(ui::UiBlendFactor factor) {
    switch (factor) {
        case ui::UiBlendFactor::One:
            return GL_ONE;
        case ui::UiBlendFactor::OneMinusSrcAlpha:
            return GL_ONE_MINUS_SRC_ALPHA;
    }
    return GL_ONE;
}

void apply_ui_blend() {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(blend_factor(ui::kUiBlend.src_color), blend_factor(ui::kUiBlend.dst_color),
            blend_factor(ui::kUiBlend.src_alpha), blend_factor(ui::kUiBlend.dst_alpha));
}

GLenum texture_unit(ui::UiTextureSlot slot) {
    return GL_TEXTURE0 + static_cast<GLenum>(slot);
}

// A store at least `needed` bytes, grown by half again so a frame a little larger than the last does not regrow it.
std::size_t grown_capacity(std::size_t current, std::size_t needed) {
    if (needed <= current) {
        return current;
    }
    const std::size_t grown = current + current / 2;
    return grown > needed ? grown : needed;
}

}

OpenGLUiDevice::OpenGLUiDevice() = default;

OpenGLUiDevice::~OpenGLUiDevice() {
    destroy();
}

bool OpenGLUiDevice::create() {
    destroy();
    program_ = std::make_unique<OpenGLShader>(adapt_glsl(kUiVertexShader, kShaderTarget, false),
            adapt_glsl(kUiFragmentShader, kShaderTarget, true));
    if (!program_->valid()) {
        program_.reset();
        return false;
    }
    program_->use();
    for (std::size_t slot = 0; slot < ui::kUiTextureSlotCount; ++slot) {
        program_->set_int(ui::kUiSamplerNames[slot], static_cast<int>(slot));
    }
    view_size_location_ = program_->uniform_location("u_view_size");
    glUseProgram(0);
    glGenBuffers(1, &vertex_buffer_);
    glGenBuffers(1, &index_buffer_);
    return vertex_buffer_ != 0 && index_buffer_ != 0;
}

void OpenGLUiDevice::destroy() {
    release_textures();
    vao_.release();
    if (vertex_buffer_ != 0) {
        glDeleteBuffers(1, &vertex_buffer_);
        vertex_buffer_ = 0;
    }
    if (index_buffer_ != 0) {
        glDeleteBuffers(1, &index_buffer_);
        index_buffer_ = 0;
    }
    vertex_capacity_ = 0;
    index_capacity_ = 0;
    program_.reset();
    view_size_location_ = -1;
}

void OpenGLUiDevice::set_up_vao() const {
    constexpr auto kStride = static_cast<GLsizei>(ui::kUiVertexStride);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    for (const ui::UiVertexAttribute& attribute : ui::kUiVertexAttributes) {
        const auto* const offset = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(attribute.offset));
        glEnableVertexAttribArray(attribute.location);
        switch (attribute.format) {
            case ui::UiVertexFormat::Float2:
                glVertexAttribPointer(attribute.location, 2, GL_FLOAT, GL_FALSE, kStride, offset);
                break;
            case ui::UiVertexFormat::Float4:
                glVertexAttribPointer(attribute.location, 4, GL_FLOAT, GL_FALSE, kStride, offset);
                break;
            case ui::UiVertexFormat::UNorm8x4:
                glVertexAttribPointer(attribute.location, 4, GL_UNSIGNED_BYTE, GL_TRUE, kStride, offset);
                break;
            case ui::UiVertexFormat::UInt:
                glVertexAttribIPointer(attribute.location, 1, GL_UNSIGNED_INT, kStride, offset);
                break;
        }
    }
    // The element buffer binding is VAO state.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
}

void OpenGLUiDevice::begin_frame(float width, float height, float pixel_ratio) {
    draw_calls_ = 0;
    bound_.fill(0);
    if (program_ == nullptr) {
        return;
    }
    glViewport(0, 0, static_cast<GLsizei>(std::lround(width * pixel_ratio)),
            static_cast<GLsizei>(std::lround(height * pixel_ratio)));
    apply_ui_blend();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    program_->use();
    glUniform2f(view_size_location_, width, height);
    // R8 rows are not 4-byte aligned; uploads read rows of the whole page (row length), offset to the region.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (std::size_t slot = 0; slot < ui::kUiTextureSlotCount; ++slot) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(slot));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void OpenGLUiDevice::create_texture(ui::UiTextureSlot slot, std::uint32_t page, int width, int height,
        ui::UiTextureFormat format, const ui::UiTextureRegion& texels) {
    std::vector<Texture>& slot_textures = textures_[static_cast<std::size_t>(slot)];
    if (slot_textures.size() <= page) {
        slot_textures.resize(static_cast<std::size_t>(page) + 1);
    }
    Texture& texture = slot_textures[page];
    if (texture.name == 0) {
        glGenTextures(1, &texture.name);
    }
    texture.format = format;
    glActiveTexture(texture_unit(slot));
    glBindTexture(GL_TEXTURE_2D, texture.name);
    bound_[static_cast<std::size_t>(slot)] = texture.name;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Base level only: without these a driver may treat the texture as incomplete for want of mipmaps.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    const bool r8 = format == ui::UiTextureFormat::R8;
    glTexImage2D(GL_TEXTURE_2D, 0, r8 ? GL_R8 : GL_RGBA8, width, height, 0, r8 ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE,
            nullptr);
    upload_region(format, texels);
}

void OpenGLUiDevice::update_texture(ui::UiTextureSlot slot, std::uint32_t page, const ui::UiTextureRegion& texels) {
    const std::vector<Texture>& slot_textures = textures_[static_cast<std::size_t>(slot)];
    if (page >= slot_textures.size() || slot_textures[page].name == 0) {
        return;
    }
    const Texture& texture = slot_textures[page];
    glActiveTexture(texture_unit(slot));
    glBindTexture(GL_TEXTURE_2D, texture.name);
    bound_[static_cast<std::size_t>(slot)] = texture.name;
    upload_region(texture.format, texels);
}

void OpenGLUiDevice::upload_region(ui::UiTextureFormat format, const ui::UiTextureRegion& texels) {
    const ui::AtlasRect& rect = texels.rect;
    if (rect.empty()) {
        return;
    }
    const std::size_t channels = format == ui::UiTextureFormat::R8 ? 1 : 4;
    const std::size_t first = (static_cast<std::size_t>(rect.y0) * static_cast<std::size_t>(texels.row_texels) +
                                      static_cast<std::size_t>(rect.x0)) *
                              channels;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, texels.row_texels);
    glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x0, rect.y0, rect.x1 - rect.x0, rect.y1 - rect.y0,
            format == ui::UiTextureFormat::R8 ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, texels.pixels.data() + first);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void OpenGLUiDevice::release_textures() {
    for (std::vector<Texture>& slot_textures : textures_) {
        for (const Texture& texture : slot_textures) {
            if (texture.name != 0) {
                glDeleteTextures(1, &texture.name);
            }
        }
        slot_textures.clear();
    }
    bound_.fill(0);
}

void OpenGLUiDevice::upload(std::span<const ui::UiVertex> vertices, std::span<const ui::UiIndex> indices) {
    if (program_ == nullptr) {
        return;
    }
    const GLuint vao = vao_.current([this] { set_up_vao(); });
    if (vao == 0) {
        return;
    }
    glBindVertexArray(vao);
    // Orphan the store each frame (same size unless it must grow), so the driver hands back fresh memory instead of
    // waiting for the GPU to finish last frame's draws from it.
    const std::size_t vertex_bytes = vertices.size_bytes();
    vertex_capacity_ = grown_capacity(vertex_capacity_, vertex_bytes);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_capacity_), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertex_bytes), vertices.data());
    const std::size_t index_bytes = indices.size_bytes();
    index_capacity_ = grown_capacity(index_capacity_, index_bytes);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_capacity_), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(index_bytes), indices.data());
}

void OpenGLUiDevice::draw(std::span<const ui::UiBatch> batches) {
    if (program_ == nullptr || vao_.current([this] { set_up_vao(); }) == 0) {
        return;
    }
    for (const ui::UiBatch& batch : batches) {
        if (batch.index_count == 0) {
            continue;
        }
        for (std::size_t slot = 0; slot < ui::kUiTextureSlotCount; ++slot) {
            const std::uint32_t page = batch.pages[slot];
            if (page == ui::UiBatch::kUnbound) {
                continue;
            }
            const std::vector<Texture>& slot_textures = textures_[slot];
            const GLuint name = page < slot_textures.size() ? slot_textures[page].name : 0;
            if (bound_[slot] != name) {
                glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(slot));
                glBindTexture(GL_TEXTURE_2D, name);
                bound_[slot] = name;
            }
        }
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(batch.index_count), GL_UNSIGNED_INT,
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(batch.index_offset) * sizeof(ui::UiIndex)));
        ++draw_calls_;
    }
}

void OpenGLUiDevice::end_frame() {
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    for (std::size_t slot = ui::kUiTextureSlotCount; slot-- > 0;) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(slot));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    bound_.fill(0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

}
