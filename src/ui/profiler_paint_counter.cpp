#include "profiler_paint_counter.h"

#if defined(ENGINE_UI_PROFILER)

#include "profile.h"

namespace engine::ui {

    ProfilerPaintCounter::ProfilerPaintCounter(const ecs::Entity &canvas, IUiPainter &inner)
        : canvas_(canvas), inner_(inner), active_(profiler_records_canvas(canvas)) {
        if (active_) {
            draw_calls_before_ = inner_.queued_draw_calls();
        }
    }

    ProfilerPaintCounter::~ProfilerPaintCounter() {
        if (active_) {
            profiler_add_paint(canvas_, commands_, inner_.queued_draw_calls() - draw_calls_before_);
        }
    }

    void ProfilerPaintCounter::save() {
        count(ProfilerPaintKind::Save);
        inner_.save();
    }

    void ProfilerPaintCounter::restore() {
        count(ProfilerPaintKind::Restore);
        inner_.restore();
    }

    void ProfilerPaintCounter::scissor(const render::Rect &rect) {
        count(ProfilerPaintKind::Scissor);
        inner_.scissor(rect);
    }

    void ProfilerPaintCounter::apply_transform(glm::vec2 center, float rotation_radians, float scale) {
        count(ProfilerPaintKind::Transform);
        inner_.apply_transform(center, rotation_radians, scale);
    }

    void ProfilerPaintCounter::apply_view(glm::vec2 origin, glm::vec2 pan, float zoom) {
        count(ProfilerPaintKind::View);
        inner_.apply_view(origin, pan, zoom);
    }

    void ProfilerPaintCounter::set_opacity(float opacity) {
        count(ProfilerPaintKind::Opacity);
        inner_.set_opacity(opacity);
    }

    void ProfilerPaintCounter::fill_rounded_rect(const render::Rect &rect, float radius, glm::vec4 color) {
        count(radius > 0.0f ? ProfilerPaintKind::FillRoundedRect : ProfilerPaintKind::FillRect);
        inner_.fill_rounded_rect(rect, radius, color);
    }

    void ProfilerPaintCounter::fill_rounded_rect_gradient(const render::Rect &rect, float radius,
                                                          const Gradient &gradient) {
        switch (gradient.kind) {
            case GradientKind::Linear:
                count(ProfilerPaintKind::LinearGradient);
                break;
            case GradientKind::Radial:
                count(ProfilerPaintKind::RadialGradient);
                break;
            case GradientKind::Conic:
                count(ProfilerPaintKind::ConicGradient);
                break;
        }
        inner_.fill_rounded_rect_gradient(rect, radius, gradient);
    }

    void ProfilerPaintCounter::stroke_rounded_rect(const render::Rect &rect, float radius, float width,
                                                   glm::vec4 color) {
        count(ProfilerPaintKind::StrokeRect);
        inner_.stroke_rounded_rect(rect, radius, width, color);
    }

    void ProfilerPaintCounter::draw_line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) {
        count(ProfilerPaintKind::Line);
        inner_.draw_line(from, to, color, width);
    }

    void ProfilerPaintCounter::stroke_arc(glm::vec2 center, float radius, float start_angle, float end_angle,
                                          float width, glm::vec4 color) {
        count(ProfilerPaintKind::Arc);
        inner_.stroke_arc(center, radius, start_angle, end_angle, width, color);
    }

    void ProfilerPaintCounter::fill_path(std::span<const PathSegment> path, glm::vec4 color) {
        count(ProfilerPaintKind::Path);
        inner_.fill_path(path, color);
    }

    void ProfilerPaintCounter::set_font(AssetId font, float size) {
        count(ProfilerPaintKind::Font);
        inner_.set_font(font, size);
    }

    void ProfilerPaintCounter::fill_text(std::string_view text, glm::vec2 position, glm::vec4 color,
                                         UiAlign horizontal, UiAlign vertical) {
        count(ProfilerPaintKind::Text);
        inner_.fill_text(text, position, color, horizontal, vertical);
    }

    void ProfilerPaintCounter::image(AssetId texture, const render::Rect &rect) {
        count(ProfilerPaintKind::Image);
        inner_.image(texture, rect);
    }

    void ProfilerPaintCounter::image_repeat(AssetId texture, const render::Rect &rect) {
        count(ProfilerPaintKind::ImageRepeat);
        inner_.image_repeat(texture, rect);
    }

    void ProfilerPaintCounter::image_nine_slice(AssetId texture, const render::Rect &rect, const BoxInsets &insets) {
        count(ProfilerPaintKind::NineSlice);
        inner_.image_nine_slice(texture, rect, insets);
    }

    glm::vec2 ProfilerPaintCounter::measure_text(std::string_view text, AssetId font, float size) {
        return inner_.measure_text(text, font, size);
    }

    TextFontMetrics ProfilerPaintCounter::font_metrics(AssetId font, float size) {
        return inner_.font_metrics(font, size);
    }

    TextBlock ProfilerPaintCounter::break_lines(std::string_view text, AssetId font, float size, float max_width) {
        return inner_.break_lines(text, font, size, max_width);
    }

} // namespace engine::ui

#endif
