#pragma once

// docs/tech/modules/UI.md (Pictures)

#include "ui/painter.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine::ui {

    // A list of engine paint commands: the calls an element subtree made on an `IUiPainter`, in order, with the
    // arguments they had. It is not a GPU command stream and not a texture: any `IUiPainter` replays it. A boundary
    // keeps one and replays it while nothing it shows changed.
    class Picture {
    public:
        struct Save {};
        struct Restore {};
        struct Scissor {
            render::Rect rect;
        };
        struct Transform {
            glm::vec2 center;
            float rotation_radians;
            float scale;
        };
        struct View {
            glm::vec2 origin;
            glm::vec2 pan;
            float zoom;
        };
        struct Opacity {
            float opacity;
        };
        struct FillRect {
            render::Rect rect;
            float radius;
            glm::vec4 color;
        };
        struct FillGradient {
            render::Rect rect;
            float radius;
            Gradient gradient;
        };
        struct StrokeRect {
            render::Rect rect;
            float radius;
            float width;
            glm::vec4 color;
        };
        struct Line {
            glm::vec2 from;
            glm::vec2 to;
            glm::vec4 color;
            float width;
        };
        struct Arc {
            glm::vec2 center;
            float radius;
            float start_angle;
            float end_angle;
            float width;
            glm::vec4 color;
        };
        struct Path {
            std::vector<PathSegment> segments;
            glm::vec4 color;
        };
        struct Font {
            AssetId font;
            float size;
        };
        struct Text {
            std::string text;
            glm::vec2 position;
            glm::vec4 color;
            UiAlign horizontal;
            UiAlign vertical;
        };
        struct Image {
            AssetId texture;
            render::Rect rect;
        };
        struct ImageRepeat {
            AssetId texture;
            render::Rect rect;
        };
        struct NineSlice {
            AssetId texture;
            render::Rect rect;
            BoxInsets insets;
        };
        // Another boundary's picture, replayed in place. It is shared: the boundary that owns it rewrites it in place,
        // and this list sees the new commands.
        struct Call {
            std::shared_ptr<const Picture> picture;
        };

        using Command = std::variant<Save, Restore, Scissor, Transform, View, Opacity, FillRect, FillGradient,
                                     StrokeRect, Line, Arc, Path, Font, Text, Image, ImageRepeat, NineSlice, Call>;

        std::vector<Command> commands;

        // Issues every command on `painter`, in order.
        void replay(IUiPainter &painter) const;

        void clear() { commands.clear(); }
        [[nodiscard]] bool empty() const noexcept { return commands.empty(); }
        // Commands in this list and in the pictures it calls.
        [[nodiscard]] std::size_t command_count() const;
    };

    // Equal when every command is equal; a `Call` is equal when it names the same picture.
    [[nodiscard]] bool operator==(const Picture &a, const Picture &b);

    // An `IUiPainter` that writes the calls into a `Picture` instead of drawing. Measuring (`measure_text`,
    // `break_lines`, `font_metrics`, `math_font`) is answered by `inner`, so text that wraps and formulas lay out as
    // they do on the real painter. No GL, so `engine_tests` use it to see what a document paints.
    class RecordingPainter final : public IUiPainter {
    public:
        RecordingPainter(IUiPainter &inner, Picture &out) : inner_(inner), out_(out) {}

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
        [[nodiscard]] TextBlock break_lines(std::string_view text, AssetId font, float size, float max_width) override;
        [[nodiscard]] int queued_draw_calls() override { return inner_.queued_draw_calls(); }

        // Records a replay of `picture` here (a boundary inside the one being recorded).
        void call(std::shared_ptr<const Picture> picture);

        // True when a non-empty string measured zero wide through this painter: a font that is not ready yet. What
        // was drawn from that measure is wrong the moment the font arrives, so the picture must not be kept.
        [[nodiscard]] bool saw_empty_measure() const noexcept { return saw_empty_measure_; }

    private:
        IUiPainter &inner_;
        Picture &out_;
        bool saw_empty_measure_ = false;
    };

    // Draws `picture` on `painter`: recorded as a call when `painter` is recording (so the outer picture follows the
    // inner one when it is rewritten), replayed otherwise.
    void draw_picture(IUiPainter &painter, const std::shared_ptr<const Picture> &picture);

} // namespace engine::ui
