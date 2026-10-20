#pragma once

#include "ui/painter.h"
#include "ui/render/affine.h"
#include "ui/render/paint_resources.h"
#include "ui/render/render_list.h"
#include "ui/render/text_layout.h"

#include <engine/builtin_ids.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace engine::ui {

// The UI renderer's painter: turns every IUiPainter call into quads of a RenderList, on the CPU, backend-neutral.
// Transform, view, opacity, and clip are resolved into the vertices (UiVertex), so a batch ends only when a texture
// slot already bound in it needs another page (RenderList::bind). docs/tech/architecture/UI Performance Plan.md, step
// 5. Pixels follow the painter wind-234 replaced:
//   - State: a stack of transform, opacity (clamped, not multiplied), clip, and font. apply_transform and apply_view
//     compose so the newest translation, rotation, or uniform scale applies to points first.
//   - Clip: the first scissor is the rect under the current transform; a nested one intersects the outer clip's
//     bounding box in the current transform's frame with the new rect. The fragment test is a one px ramp centered on
//     the edge, from the vertex's clip-frame position, so a rotated clip is exact without a stencil. A quad wholly
//     outside the clip is dropped.
//   - Shapes: signed-distance quads with a one px fringe; strokes are centered on the edge with the thin-stroke rule
//     (width under one px draws one px wide at alpha width^2) and the 200 px cap; lines and arcs have butt caps;
//     corners under 0.1 are sharp.
//   - fill_path and stroke_path: rasterized into the mask atlas at the transform's exact rotation and scale and a
//     quarter-pixel phase. A stroke also keys the cache on its device-pixel width.
//   - Text: FontLibrary glyph quads at the text raster scale, corners transformed, rows swapped under a mirroring
//     transform.
// Units: positions are logical (begin_frame's width x height); "px" in UiVertex are logical * pixel ratio * transform
// scale.
class BatchingPainter final : public IUiPainter {
public:
    explicit BatchingPainter(PaintResources& resources);

    // Starts a list for a `width` x `height` (logical) target. Clears the list, keeping its capacity, and resets the
    // state to identity, no clip, opacity 1, the builtin UI font at 16.
    void begin_frame(float width, float height, float pixel_ratio = 1.0f);
    // The list is complete; a device draws `list()` now.
    void end_frame();
    [[nodiscard]] const RenderList& list() const noexcept { return list_; }
    [[nodiscard]] PaintResources& resources() noexcept { return resources_; }
    [[nodiscard]] float width() const noexcept { return width_; }
    [[nodiscard]] float height() const noexcept { return height_; }
    [[nodiscard]] float pixel_ratio() const noexcept { return pixel_ratio_; }

    [[nodiscard]] const math::MathFont* math_font() const override { return resources_.fonts.math_font(); }
    void save() override;
    void restore() override;
    void scissor(const render::Rect& rect) override;
    void apply_transform(glm::vec2 center, float rotation_radians, float scale) override;
    void apply_view(glm::vec2 origin, glm::vec2 pan, float zoom) override;
    void set_opacity(float opacity) override;
    void fill_rounded_rect(const render::Rect& rect, float radius, glm::vec4 color) override;
    void fill_rounded_rect_gradient(const render::Rect& rect, float radius, const Gradient& gradient) override;
    void stroke_rounded_rect(const render::Rect& rect, float radius, float width, glm::vec4 color) override;
    void draw_line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) override;
    void stroke_arc(glm::vec2 center, float radius, float start_angle, float end_angle, float width,
            glm::vec4 color) override;
    void fill_path(std::span<const PathSegment> path, glm::vec4 color) override;
    void stroke_path(std::span<const PathSegment> path, float width, glm::vec4 color) override;
    void set_font(AssetId font, float size) override;
    void fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, UiAlign horizontal,
            UiAlign vertical) override;
    void image(AssetId texture, const render::Rect& rect) override;
    void image_repeat(AssetId texture, const render::Rect& rect) override;
    void image_nine_slice(AssetId texture, const render::Rect& rect, const BoxInsets& insets) override;
    [[nodiscard]] glm::vec2 measure_text(std::string_view text, AssetId font, float size) override;
    [[nodiscard]] TextFontMetrics font_metrics(AssetId font, float size) override;
    [[nodiscard]] TextBlock break_lines(std::string_view text, AssetId font, float size, float max_width) override;
    // Batches so far this frame: the draw calls the device will issue.
    [[nodiscard]] int queued_draw_calls() override { return static_cast<int>(list_.batches().size()); }

private:
    struct State {
        Affine2 xform;
        float alpha = 1.0f;
        AssetId font = builtin::font_ui;
        float font_size = 16.0f;
        bool clipped = false;
        // Clip rect frame (origin at its center) to logical units, and its half extent in that frame.
        Affine2 clip_xform;
        glm::vec2 clip_extent{};
        // Derived: logical to the clip frame in px, the extent in px, and the clip's logical bounding box.
        Affine2 to_clip_px;
        glm::vec2 clip_extent_px{};
        glm::vec2 clip_min{};
        glm::vec2 clip_max{};
    };

    // One quad before it becomes vertices. Corners go around the quad (top left, top right, bottom right, bottom
    // left in the shape's frame); `position` is logical.
    struct Quad {
        glm::vec2 position[4];
        glm::vec2 local[4];
        glm::vec2 uv[4];
        glm::vec4 shape{};
        glm::vec2 aux{};
        std::uint32_t color = 0;
        UiShapeKind kind = UiShapeKind::Fill;
        bool textured = false;
        UiTextureSlot slot = UiTextureSlot::Glyph;
        std::uint32_t page = 0;
    };

    void set_clip(const render::Rect& rect);
    // Logical px per local unit of the current transform (its average scale times the pixel ratio).
    [[nodiscard]] float px_scale() const noexcept;
    // The stroke half width in px after the 200 px cap and the thin-stroke rule; scales `alpha` for a thin stroke.
    [[nodiscard]] float stroke_half_width(float width, float& alpha) const noexcept;
    // A box quad of `rect` (normalized) grown by `grow` local units plus the antialiasing margin; `uv_map` maps local
    // points to uv. `local` and `shape.xy` are filled from the rect; the caller fills the rest of `quad` first.
    void push_box(const render::Rect& rect, float grow, const Affine2& uv_map, Quad& quad);
    void push(Quad& quad);
    // An Image or ImageRepeat quad of `img` at the current opacity, its geometry still to fill.
    [[nodiscard]] Quad image_quad(const AtlasImage& img, UiShapeKind kind) const;

    PaintResources& resources_;
    RenderList list_;
    State state_;
    std::vector<State> stack_;
    std::vector<GlyphQuad> glyphs_;
    float width_ = 0.0f;
    float height_ = 0.0f;
    float pixel_ratio_ = 1.0f;
};

}
