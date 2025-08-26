#pragma once

#if defined(ENGINE_UI_PROFILER)

#include "ui/painter.h"

#include <engine/ecs/entity.h>
#include <engine/ui/profiler.h>

#include <array>
#include <span>
#include <string_view>

namespace engine::ui {

    // Counts one canvas's painter calls by kind and the GPU draw calls they queue, for the UI profiler. Paint
    // draws through `painter()`: the wrapped painter itself when the canvas is not recorded, so an unprofiled
    // paint adds no indirection; this counter when it is, forwarding every call. The destructor adds the counts
    // to the canvas's open frame. Layout keeps the wrapped painter: its dirty gate compares painter identity.
    class ProfilerPaintCounter final : public IUiPainter {
    public:
        ProfilerPaintCounter(const ecs::Entity &canvas, IUiPainter &inner);
        ~ProfilerPaintCounter() override;

        ProfilerPaintCounter(const ProfilerPaintCounter &) = delete;
        ProfilerPaintCounter &operator=(const ProfilerPaintCounter &) = delete;

        [[nodiscard]] IUiPainter &painter() { return active_ ? static_cast<IUiPainter &>(*this) : inner_; }

        [[nodiscard]] const math::MathFont *math_font() const override { return inner_.math_font(); }
        void save() override;
        void restore() override;
        void scissor(const render::Rect &rect) override;
        void apply_transform(glm::vec2 center, float rotation_radians, float scale) override;
        void apply_view(glm::vec2 origin, glm::vec2 pan, float zoom) override;
        void set_opacity(float opacity) override;
        void fill_rounded_rect(const render::Rect &rect, float radius, glm::vec4 color) override;
        void fill_rounded_rect_gradient(const render::Rect &rect, float radius, const Gradient &gradient) override;
        void stroke_rounded_rect(const render::Rect &rect, float radius, float width, glm::vec4 color) override;
        void draw_line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) override;
        void stroke_arc(glm::vec2 center, float radius, float start_angle, float end_angle, float width,
                        glm::vec4 color) override;
        void fill_path(std::span<const PathSegment> path, glm::vec4 color) override;
        void set_font(AssetId font, float size) override;
        void fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, UiAlign horizontal,
                       UiAlign vertical) override;
        void image(AssetId texture, const render::Rect &rect) override;
        void image_repeat(AssetId texture, const render::Rect &rect) override;
        void image_nine_slice(AssetId texture, const render::Rect &rect, const BoxInsets &insets) override;
        [[nodiscard]] glm::vec2 measure_text(std::string_view text, AssetId font, float size) override;
        [[nodiscard]] TextFontMetrics font_metrics(AssetId font, float size) override;
        [[nodiscard]] TextBlock break_lines(std::string_view text, AssetId font, float size,
                                            float max_width) override;
        [[nodiscard]] int queued_draw_calls() override { return inner_.queued_draw_calls(); }

    private:
        void count(ProfilerPaintKind kind) { ++commands_[static_cast<std::size_t>(kind)]; }

        const ecs::Entity &canvas_;
        IUiPainter &inner_;
        std::array<int, kProfilerPaintKindCount> commands_{};
        int draw_calls_before_ = 0;
        bool active_ = false;
    };

} // namespace engine::ui

#endif
