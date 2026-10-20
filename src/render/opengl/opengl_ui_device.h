#pragma once

#include "context_vao.h"
#include "gl_includes.h"

#include "ui/render/ui_device.h"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace engine::render {

class OpenGLShader;

// IUiDevice on OpenGL 3.3 core and OpenGL ES 3.0 (ENGINE_WITH_GLES; WebGL2 on the web). One per window canvas, made
// and destroyed with that window's context current. Its program, buffers, and textures are its own; its VAO goes
// through ContextVao like every other VAO (wind-233), though it is only ever drawn in its own context.
//
// State: begin_frame sets everything ui_pipeline.h asks for (program, VAO, premultiplied blend, no depth, stencil,
// cull, or scissor, the four sampler units); end_frame leaves what opengl_backend.cpp expects
// of the next frame: blend on with the premultiplied function, depth, stencil, cull, and scissor off, program 0,
// VAO 0, array buffer 0, texture units 0..3 bound to 0 with unit 0 active, unpack alignment 4 and row length 0.
// Nothing is queried back from the driver (no glGet in a frame).
class OpenGLUiDevice final : public ui::IUiDevice {
public:
    OpenGLUiDevice();
    ~OpenGLUiDevice() override;

    OpenGLUiDevice(const OpenGLUiDevice&) = delete;
    OpenGLUiDevice& operator=(const OpenGLUiDevice&) = delete;

    // Compiles the program and makes the buffers in the current context. False when the program does not link.
    [[nodiscard]] bool create();

    void begin_frame(float width, float height, float pixel_ratio) override;
    void create_texture(ui::UiTextureSlot slot, std::uint32_t page, int width, int height, ui::UiTextureFormat format,
            const ui::UiTextureRegion& texels) override;
    void update_texture(ui::UiTextureSlot slot, std::uint32_t page, const ui::UiTextureRegion& texels) override;
    void release_textures() override;
    void upload(std::span<const ui::UiVertex> vertices, std::span<const ui::UiIndex> indices) override;
    void draw(std::span<const ui::UiBatch> batches) override;
    void end_frame() override;
    [[nodiscard]] int draw_calls() const override { return draw_calls_; }

private:
    struct Texture {
        GLuint name = 0;
        ui::UiTextureFormat format = ui::UiTextureFormat::R8;
    };

    void destroy();
    void set_up_vao() const;
    // Uploads `texels` into the texture bound to GL_TEXTURE_2D on the active unit.
    static void upload_region(ui::UiTextureFormat format, const ui::UiTextureRegion& texels);

    std::unique_ptr<OpenGLShader> program_;
    GLint view_size_location_ = -1;
    ContextVao vao_;
    GLuint vertex_buffer_ = 0;
    GLuint index_buffer_ = 0;
    // Buffer store sizes in bytes; a frame that fits orphans the store at the same size, a larger one grows it.
    std::size_t vertex_capacity_ = 0;
    std::size_t index_capacity_ = 0;
    std::array<std::vector<Texture>, ui::kUiTextureSlotCount> textures_;
    // The texture bound on each unit since begin_frame, so a batch rebinds only what changed.
    std::array<GLuint, ui::kUiTextureSlotCount> bound_{};
    int draw_calls_ = 0;
};

}
