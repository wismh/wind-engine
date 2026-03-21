#pragma once

#include "ui/painter.h"
#include "ui/render/batching_painter.h"
#include "ui/render/paint_resources.h"
#include "ui/render/ui_device.h"
#include "ui/render/ui_texture_sync.h"

#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/font.h>

namespace engine::ui {

// The UI painter a canvas owns: a BatchingPainter over the PaintResources every window shares, and the canvas's own
// device. Painter calls build the frame's RenderList on the CPU; end_frame brings the device's textures up to date
// and draws the list. docs/tech/architecture/UI Performance Plan.md, step 5 (Design, wind-234). Backend-neutral: the
// canvas hands in the device, and the device is the only part that knows the GPU API.
//
// Fonts and images register into the shared resources, once, whichever window registers them first; every other
// window's add_font / add_image of the same id is a lookup.
class UiRenderer final : public IUiPainter {
public:
    UiRenderer(PaintResources& resources, IUiDevice& device);

    // add_font for builtin::font_ui, the fallback face of every font id that is not registered.
    [[nodiscard]] bool load_ui_font(const Font& font);
    [[nodiscard]] bool add_font(AssetId id, const Font& font);
    [[nodiscard]] bool add_image(AssetId id, const render::TextureDesc& desc);
    // Forgets every registered image, in every window (PaintResources::drop_images); fonts stay.
    void drop_images();

    // Starts the frame's list for a `width` x `height` (logical) target. No device call.
    void begin_frame(float width, float height, float pixel_ratio = 1.0f);
    // Submits the list: catches the device's textures up with the shared pages, uploads the geometry, and draws one
    // call per batch. A list without a quad, or an empty target, makes no device call.
    void end_frame();

    [[nodiscard]] const RenderList& list() const noexcept { return painter_.list(); }
    [[nodiscard]] PaintResources& resources() noexcept { return painter_.resources(); }

    [[nodiscard]] const math::MathFont* math_font() const override { return painter_.math_font(); }
    void save() override { painter_.save(); }
    void restore() override { painter_.restore(); }
    void scissor(const render::Rect& rect) override { painter_.scissor(rect); }
    void apply_transform(glm::vec2 center, float rotation_radians, float scale) override {
        painter_.apply_transform(center, rotation_radians, scale);
    }
    void apply_view(glm::vec2 origin, glm::vec2 pan, float zoom) override { painter_.apply_view(origin, pan, zoom); }
    void set_opacity(float opacity) override { painter_.set_opacity(opacity); }
    void fill_rounded_rect(const render::Rect& rect, float radius, glm::vec4 color) override {
        painter_.fill_rounded_rect(rect, radius, color);
    }
    void fill_rounded_rect_gradient(const render::Rect& rect, float radius, const Gradient& gradient) override {
        painter_.fill_rounded_rect_gradient(rect, radius, gradient);
    }
    void stroke_rounded_rect(const render::Rect& rect, float radius, float width, glm::vec4 color) override {
        painter_.stroke_rounded_rect(rect, radius, width, color);
    }
    void draw_line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) override {
        painter_.draw_line(from, to, color, width);
    }
    void stroke_arc(glm::vec2 center, float radius, float start_angle, float end_angle, float width,
            glm::vec4 color) override {
        painter_.stroke_arc(center, radius, start_angle, end_angle, width, color);
    }
    void fill_path(std::span<const PathSegment> path, glm::vec4 color) override { painter_.fill_path(path, color); }
    void set_font(AssetId font, float size) override { painter_.set_font(font, size); }
    void fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, UiAlign horizontal,
            UiAlign vertical) override {
        painter_.fill_text(text, position, color, horizontal, vertical);
    }
    void image(AssetId texture, const render::Rect& rect) override { painter_.image(texture, rect); }
    void image_repeat(AssetId texture, const render::Rect& rect) override { painter_.image_repeat(texture, rect); }
    void image_nine_slice(AssetId texture, const render::Rect& rect, const BoxInsets& insets) override {
        painter_.image_nine_slice(texture, rect, insets);
    }
    [[nodiscard]] glm::vec2 measure_text(std::string_view text, AssetId font, float size) override {
        return painter_.measure_text(text, font, size);
    }
    [[nodiscard]] TextFontMetrics font_metrics(AssetId font, float size) override {
        return painter_.font_metrics(font, size);
    }
    [[nodiscard]] TextBlock break_lines(std::string_view text, AssetId font, float size, float max_width) override {
        return painter_.break_lines(text, font, size, max_width);
    }
    // The batch count so far this frame: the draw calls end_frame issues (the UI profiler's draw_calls).
    [[nodiscard]] int queued_draw_calls() override { return painter_.queued_draw_calls(); }

private:
    BatchingPainter painter_;
    IUiDevice& device_;
    UiTextureSync textures_;
};

}
