#pragma once

#include <engine/core/window_desc.h>
#include <engine/ecs/world.h>
#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include "ui/text_wrap.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui::math {
    class MathFont;
}

namespace engine::ui {

    // Vertical metrics of a UI font at one size, in the same units as `measure_text`. `ascent` is the
    // distance from the baseline up to the top of the em box; `descent` is the distance from the
    // baseline down. `line_height` is the font's own row stride.
    struct TextFontMetrics {
        float ascent = 0.0f;
        float descent = 0.0f;
        float line_height = 0.0f;
    };

    // One segment of a filled path. A `Move` starts a new contour (the previous one closes implicitly, like a
    // glyph outline); `Quad` curves through control point `c1`, `Cubic` through `c1` and `c2`. All coordinates
    // are real pixels.
    struct PathSegment {
        enum class Kind : std::uint8_t {
            Move,
            Line,
            Quad,
            Cubic,
        };

        Kind kind = Kind::Move;
        glm::vec2 p{};
        glm::vec2 c1{};
        glm::vec2 c2{};
    };

    class IUiPainter {
    public:
        virtual ~IUiPainter() = default;

        // The OpenType MATH font `<Math>` formulas are laid out and drawn with, or nullptr until the painter has been
        // given one (layout then falls back to a rough size, like text measured without a painter). Layout reads
        // glyph metrics from it directly, so formula sizes do not depend on a GPU.
        [[nodiscard]] virtual const math::MathFont *math_font() const { return nullptr; }

        virtual void save() = 0;
        virtual void restore() = 0;
        virtual void scissor(const render::Rect &rect) = 0;
        // Rotates (radians) and uniformly scales everything painted after this call - including
        // recursed children - about `center`, until the matching restore(). No un-apply call is
        // needed: restore() already undoes it, the same way a single scissor() call has no
        // matching "unscissor".
        virtual void apply_transform(glm::vec2 center, float rotation_radians, float scale) = 0;
        // Paint-time Viewport camera: `displayed = origin + zoom * (layout - origin + pan)`.
        // `pan` is in the same units as `origin` (not pre-multiplied by zoom).
        virtual void apply_view(glm::vec2 origin, glm::vec2 pan, float zoom) = 0;
        virtual void set_opacity(float opacity) = 0;
        virtual void fill_rounded_rect(const render::Rect &rect, float radius, glm::vec4 color) = 0;
        virtual void fill_rounded_rect_gradient(const render::Rect &rect, float radius, const Gradient &gradient) = 0;
        virtual void stroke_rounded_rect(const render::Rect &rect, float radius, float width, glm::vec4 color) = 0;
        virtual void draw_line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) = 0;
        // See IDrawList::arc (draw_list.h) for the angle convention (radians, 0 = 12 o'clock, clockwise).
        virtual void stroke_arc(glm::vec2 center, float radius, float start_angle, float end_angle, float width,
                                glm::vec4 color) = 0;
        // Fills `path` in one go. A contour wound the same way as the path's largest contour is solid; one wound
        // the opposite way is a hole cut out of what it sits in (how font outlines encode counters like the inside
        // of an `o`), whatever the font's own outer-contour direction is. Overlapping solid contours of one path
        // union, which is why every glyph of a formula can share a single path.
        virtual void fill_path(std::span<const PathSegment> path, glm::vec4 color) = 0;
        virtual void set_font(AssetId font, float size) = 0;
        virtual void fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, UiAlign horizontal,
                               UiAlign vertical) = 0;
        virtual void image(AssetId texture, const render::Rect &rect) = 0;
        // Tiles the texture at its native pixel size across `rect` instead of stretching it to fit -
        // for `background-repeat: repeat` (pixel-art backgrounds that must not blur).
        virtual void image_repeat(AssetId texture, const render::Rect &rect) = 0;
        virtual void image_nine_slice(AssetId texture, const render::Rect &rect, const BoxInsets &insets) = 0;
        [[nodiscard]] virtual glm::vec2 measure_text(std::string_view text, AssetId font, float size) = 0;
        // Ascent, descent and line height of `font` at `size`. The default splits `measure_text`'s height
        // into a rough 80/20 ascent/descent; a backend with real font metrics overrides it. Inline math
        // uses this so a formula and the surrounding letters share a baseline.
        [[nodiscard]] virtual TextFontMetrics font_metrics(AssetId font, float size);
        // Breaks `text` into rows no wider than `max_width` at `font`/`size` (see break_text_lines). The default wraps
        // through measure_text, so a painter only overrides it to use its own shaper's line breaker.
        [[nodiscard]] virtual TextBlock break_lines(std::string_view text, AssetId font, float size, float max_width);
    };

    struct UiPaintInput {
        render::Rect canvas_rect{};
        glm::vec2 pointer{};
        bool pointer_down = false;
        float delta_time = 0.0f;
        float window_width = 0.0f;
        float window_height = 0.0f;
        glm::vec2 ui_offset{0.0f, 0.0f}; // canvas_rect-space -> real-pixel offset (identity for FillWindow/Fixed)
        float ui_scale = 1.0f; // canvas_rect-space -> real-pixel scale
        // Set by run_ui_render while the UI inspector is attached. Hover is the canvas under the pointer;
        // selection is the picked path on this canvas (empty path is the root).
        bool inspector_hover = false;
        bool inspector_selection = false;
        std::vector<std::size_t> inspector_selection_path;
        const void *inspector_selection_owner = nullptr;
        // Copied from CmdDrawUI. Empty unless the canvas's world is the one the UI profiler records.
        ecs::Entity canvas{};
    };

    // Identity of the math font `painter` (possibly null) would lay formulas out with, for the layout dirty-gate.
    [[nodiscard]] inline const void *math_font_identity(const IUiPainter *painter) noexcept {
        return painter != nullptr ? static_cast<const void *>(painter->math_font()) : nullptr;
    }

    void apply_layout_style(Element &root, const Stylesheet *sheet, float window_width = 0.0f,
                            float window_height = 0.0f);
    // `partial` re-packs only elements whose layout inputs (or a descendant's) changed and translates
    // siblings whose size stayed put. The full walk is the default.
    void layout(UiDocument &document, const render::Rect &canvas_rect, IUiPainter *painter, bool partial = false);
    void paint_document(UiDocument &document, const Stylesheet *stylesheet, IUiPainter &painter,
                        const UiPaintInput &input);

    // Rules that match `element` with pseudo-classes, in cascade order (lowest specificity first,
    // source order breaks ties). The last entry is the one that wins. Empty when `sheet` is null.
    struct MatchedRule {
        std::string selector;
        std::vector<CssDeclaration> declarations;
        int specificity = 0;
        std::size_t index = 0;
    };

    [[nodiscard]] std::vector<MatchedRule> match_style_rules(const Element &element, const Stylesheet *sheet,
                                                             const std::vector<const Element *> &ancestors,
                                                             float window_width, float window_height);

    // The rows a Label/Button's text is drawn in when it wraps (`white-space: normal`) at `content_width` (design
    // pixels, the laid-out content box), or nullptr when it is one row and is drawn as a whole. Reuses the rows layout
    // already broke the text into, so paint and the height layout reserved cannot disagree; rows broken at a width at
    // least `content_width` that still fit it are the same rows. The pointer is into `element`'s wrap cache and lives
    // until the element is next measured.
    [[nodiscard]] const TextBlock *wrapped_text_rows(const Element &element, IUiPainter &painter, float font_size,
                                                     float content_width);

    // wind-129 layout dirty-gate (document.cpp): true if any layout-relevant field changed anywhere in
    // `element`'s subtree (text, custom_properties, or ItemsControl generated_owner sequence) since the
    // last call on the same Element(s) — see Element's layout_dirty_check_* fields (document.h) for why
    // this narrow set is exactly what layout depends on. Not yet wired into prepare_top_canvas/
    // paint_document; declared here (like apply_layout_style/layout above) purely so tests linking
    // against `engine` can call it without a second public entry point.
    [[nodiscard]] bool layout_state_changed(Element &element);

    // Per-window painter used by hit-test layout so hug text metrics match paint_document.
    // Unset / empty resolve keeps the CPU fallback (engine_tests, windows with no UI painter).
    struct UiLayoutPainters {
        std::function<IUiPainter *(WindowId)> resolve;
    };

    [[nodiscard]] inline IUiPainter *layout_painter_for(ecs::World &world, WindowId window) {
        const UiLayoutPainters &painters = world.ctx<UiLayoutPainters>();
        if (!painters.resolve) {
            return nullptr;
        }
        return painters.resolve(window);
    }

    // Layout/design space -> real-pixel space, the same conversion paint_document applies to every
    // Element::layout_rect before drawing it (via UiPaintInput::ui_offset/ui_scale) and hit-test
    // applies to a canvas's content (via UiCanvasSpace::offset/scale, canvas.h) — one shared
    // definition so a click's hit-tested position and what actually got painted can't disagree.
    [[nodiscard]] inline render::Rect scale_rect(const render::Rect &rect, glm::vec2 offset, float scale) noexcept {
        return render::Rect{
                offset.x + rect.x * scale,
                offset.y + rect.y * scale,
                rect.w * scale,
                rect.h * scale,
        };
    }

    // x where a TextInput/Label/Button's text (and TextInput's caret/selection) starts, given its
    // content-box x/width and cascaded text-align — the same three-way branch paint.cpp's TextInput
    // block and canvas.cpp's click-to-caret-index both need. Shared so they can never compute it
    // differently and disagree about where a click's index falls versus where the caret renders.
    // Kind, then `#id` when set, then `.class` for each class. Shared by the inspector tree and the
    // on-canvas badge.
    [[nodiscard]] std::string inspector_element_tag(const Element &element);

    [[nodiscard]] inline float text_align_origin_x(float content_x, float content_w, UiAlign text_align) noexcept {
        if (text_align == UiAlign::Center) {
            return content_x + content_w * 0.5f;
        }
        if (text_align == UiAlign::End) {
            return content_x + content_w;
        }
        return content_x;
    }

} // namespace engine::ui
