#include "ui/picture.h"

#include <type_traits>

namespace engine::ui {

    namespace {

        bool same_gradient(const Gradient &a, const Gradient &b) {
            if (a.kind != b.kind || a.angle_deg != b.angle_deg || a.stops.size() != b.stops.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.stops.size(); ++i) {
                if (a.stops[i].color != b.stops[i].color || a.stops[i].percent != b.stops[i].percent) {
                    return false;
                }
            }
            return true;
        }

        bool same_path(const std::vector<PathSegment> &a, const std::vector<PathSegment> &b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (a[i].kind != b[i].kind || a[i].p != b[i].p || a[i].c1 != b[i].c1 || a[i].c2 != b[i].c2) {
                    return false;
                }
            }
            return true;
        }

        bool same_insets(const BoxInsets &a, const BoxInsets &b) {
            return a.top == b.top && a.right == b.right && a.bottom == b.bottom && a.left == b.left;
        }

        bool same_command(const Picture::Command &a, const Picture::Command &b) {
            if (a.index() != b.index()) {
                return false;
            }
            return std::visit(
                    [&](const auto &x) {
                        using T = std::decay_t<decltype(x)>;
                        const T &y = std::get<T>(b);
                        if constexpr (std::is_same_v<T, Picture::Save> || std::is_same_v<T, Picture::Restore>) {
                            return true;
                        } else if constexpr (std::is_same_v<T, Picture::Scissor>) {
                            return x.rect == y.rect;
                        } else if constexpr (std::is_same_v<T, Picture::Transform>) {
                            return x.center == y.center && x.rotation_radians == y.rotation_radians &&
                                   x.scale == y.scale;
                        } else if constexpr (std::is_same_v<T, Picture::View>) {
                            return x.origin == y.origin && x.pan == y.pan && x.zoom == y.zoom;
                        } else if constexpr (std::is_same_v<T, Picture::Opacity>) {
                            return x.opacity == y.opacity;
                        } else if constexpr (std::is_same_v<T, Picture::FillRect>) {
                            return x.rect == y.rect && x.radius == y.radius && x.color == y.color;
                        } else if constexpr (std::is_same_v<T, Picture::FillGradient>) {
                            return x.rect == y.rect && x.radius == y.radius && same_gradient(x.gradient, y.gradient);
                        } else if constexpr (std::is_same_v<T, Picture::StrokeRect>) {
                            return x.rect == y.rect && x.radius == y.radius && x.width == y.width && x.color == y.color;
                        } else if constexpr (std::is_same_v<T, Picture::Line>) {
                            return x.from == y.from && x.to == y.to && x.color == y.color && x.width == y.width;
                        } else if constexpr (std::is_same_v<T, Picture::Arc>) {
                            return x.center == y.center && x.radius == y.radius && x.start_angle == y.start_angle &&
                                   x.end_angle == y.end_angle && x.width == y.width && x.color == y.color;
                        } else if constexpr (std::is_same_v<T, Picture::Path>) {
                            return same_path(x.segments, y.segments) && x.color == y.color;
                        } else if constexpr (std::is_same_v<T, Picture::StrokePath>) {
                            return same_path(x.segments, y.segments) && x.width == y.width && x.color == y.color;
                        } else if constexpr (std::is_same_v<T, Picture::Font>) {
                            return x.font == y.font && x.size == y.size;
                        } else if constexpr (std::is_same_v<T, Picture::Text>) {
                            return x.text == y.text && x.position == y.position && x.color == y.color &&
                                   x.horizontal == y.horizontal && x.vertical == y.vertical;
                        } else if constexpr (std::is_same_v<T, Picture::Image> ||
                                             std::is_same_v<T, Picture::ImageRepeat>) {
                            return x.texture == y.texture && x.rect == y.rect;
                        } else if constexpr (std::is_same_v<T, Picture::NineSlice>) {
                            return x.texture == y.texture && x.rect == y.rect && same_insets(x.insets, y.insets);
                        } else {
                            static_assert(std::is_same_v<T, Picture::Call>);
                            return x.picture == y.picture;
                        }
                    },
                    a);
        }

    } // namespace

    bool operator==(const Picture &a, const Picture &b) {
        if (a.commands.size() != b.commands.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.commands.size(); ++i) {
            if (!same_command(a.commands[i], b.commands[i])) {
                return false;
            }
        }
        return true;
    }

    void Picture::replay(IUiPainter &painter) const {
        for (const Command &command: commands) {
            std::visit(
                    [&](const auto &c) {
                        using T = std::decay_t<decltype(c)>;
                        if constexpr (std::is_same_v<T, Save>) {
                            painter.save();
                        } else if constexpr (std::is_same_v<T, Restore>) {
                            painter.restore();
                        } else if constexpr (std::is_same_v<T, Scissor>) {
                            painter.scissor(c.rect);
                        } else if constexpr (std::is_same_v<T, Transform>) {
                            painter.apply_transform(c.center, c.rotation_radians, c.scale);
                        } else if constexpr (std::is_same_v<T, View>) {
                            painter.apply_view(c.origin, c.pan, c.zoom);
                        } else if constexpr (std::is_same_v<T, Opacity>) {
                            painter.set_opacity(c.opacity);
                        } else if constexpr (std::is_same_v<T, FillRect>) {
                            painter.fill_rounded_rect(c.rect, c.radius, c.color);
                        } else if constexpr (std::is_same_v<T, FillGradient>) {
                            painter.fill_rounded_rect_gradient(c.rect, c.radius, c.gradient);
                        } else if constexpr (std::is_same_v<T, StrokeRect>) {
                            painter.stroke_rounded_rect(c.rect, c.radius, c.width, c.color);
                        } else if constexpr (std::is_same_v<T, Line>) {
                            painter.draw_line(c.from, c.to, c.color, c.width);
                        } else if constexpr (std::is_same_v<T, Arc>) {
                            painter.stroke_arc(c.center, c.radius, c.start_angle, c.end_angle, c.width, c.color);
                        } else if constexpr (std::is_same_v<T, Path>) {
                            painter.fill_path(c.segments, c.color);
                        } else if constexpr (std::is_same_v<T, StrokePath>) {
                            painter.stroke_path(c.segments, c.width, c.color);
                        } else if constexpr (std::is_same_v<T, Font>) {
                            painter.set_font(c.font, c.size);
                        } else if constexpr (std::is_same_v<T, Text>) {
                            painter.fill_text(c.text, c.position, c.color, c.horizontal, c.vertical);
                        } else if constexpr (std::is_same_v<T, Image>) {
                            painter.image(c.texture, c.rect);
                        } else if constexpr (std::is_same_v<T, ImageRepeat>) {
                            painter.image_repeat(c.texture, c.rect);
                        } else if constexpr (std::is_same_v<T, NineSlice>) {
                            painter.image_nine_slice(c.texture, c.rect, c.insets);
                        } else {
                            static_assert(std::is_same_v<T, Call>);
                            if (c.picture != nullptr) {
                                c.picture->replay(painter);
                            }
                        }
                    },
                    command);
        }
    }

    std::size_t Picture::command_count() const {
        std::size_t count = 0;
        for (const Command &command: commands) {
            ++count;
            if (const auto *call = std::get_if<Call>(&command); call != nullptr && call->picture != nullptr) {
                count += call->picture->command_count();
            }
        }
        return count;
    }

    void RecordingPainter::save() { out_.commands.emplace_back(Picture::Save{}); }

    void RecordingPainter::restore() { out_.commands.emplace_back(Picture::Restore{}); }

    void RecordingPainter::scissor(const render::Rect &rect) { out_.commands.emplace_back(Picture::Scissor{rect}); }

    void RecordingPainter::apply_transform(glm::vec2 center, float rotation_radians, float scale) {
        out_.commands.emplace_back(Picture::Transform{center, rotation_radians, scale});
    }

    void RecordingPainter::apply_view(glm::vec2 origin, glm::vec2 pan, float zoom) {
        out_.commands.emplace_back(Picture::View{origin, pan, zoom});
    }

    void RecordingPainter::set_opacity(float opacity) { out_.commands.emplace_back(Picture::Opacity{opacity}); }

    void RecordingPainter::fill_rounded_rect(const render::Rect &rect, float radius, glm::vec4 color) {
        out_.commands.emplace_back(Picture::FillRect{rect, radius, color});
    }

    void RecordingPainter::fill_rounded_rect_gradient(const render::Rect &rect, float radius,
                                                      const Gradient &gradient) {
        out_.commands.emplace_back(Picture::FillGradient{rect, radius, gradient});
    }

    void RecordingPainter::stroke_rounded_rect(const render::Rect &rect, float radius, float width, glm::vec4 color) {
        out_.commands.emplace_back(Picture::StrokeRect{rect, radius, width, color});
    }

    void RecordingPainter::draw_line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) {
        out_.commands.emplace_back(Picture::Line{from, to, color, width});
    }

    void RecordingPainter::stroke_arc(glm::vec2 center, float radius, float start_angle, float end_angle, float width,
                                      glm::vec4 color) {
        out_.commands.emplace_back(Picture::Arc{center, radius, start_angle, end_angle, width, color});
    }

    void RecordingPainter::fill_path(std::span<const PathSegment> path, glm::vec4 color) {
        out_.commands.emplace_back(Picture::Path{std::vector<PathSegment>(path.begin(), path.end()), color});
    }

    void RecordingPainter::stroke_path(std::span<const PathSegment> path, float width, glm::vec4 color) {
        out_.commands.emplace_back(
                Picture::StrokePath{std::vector<PathSegment>(path.begin(), path.end()), width, color});
    }

    void RecordingPainter::set_font(AssetId font, float size) { out_.commands.emplace_back(Picture::Font{font, size}); }

    void RecordingPainter::fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, UiAlign horizontal,
                                     UiAlign vertical) {
        out_.commands.emplace_back(Picture::Text{std::string(text), position, color, horizontal, vertical});
    }

    void RecordingPainter::image(AssetId texture, const render::Rect &rect) {
        out_.commands.emplace_back(Picture::Image{texture, rect});
    }

    void RecordingPainter::image_repeat(AssetId texture, const render::Rect &rect) {
        out_.commands.emplace_back(Picture::ImageRepeat{texture, rect});
    }

    void RecordingPainter::image_nine_slice(AssetId texture, const render::Rect &rect, const BoxInsets &insets) {
        out_.commands.emplace_back(Picture::NineSlice{texture, rect, insets});
    }

    glm::vec2 RecordingPainter::measure_text(std::string_view text, AssetId font, float size) {
        const glm::vec2 measured = inner_.measure_text(text, font, size);
        if (!text.empty() && measured.x <= 0.0f) {
            saw_empty_measure_ = true;
        }
        return measured;
    }

    TextFontMetrics RecordingPainter::font_metrics(AssetId font, float size) { return inner_.font_metrics(font, size); }

    TextBlock RecordingPainter::break_lines(std::string_view text, AssetId font, float size, float max_width) {
        return inner_.break_lines(text, font, size, max_width);
    }

    void RecordingPainter::call(std::shared_ptr<const Picture> picture) {
        out_.commands.emplace_back(Picture::Call{std::move(picture)});
    }

    void draw_picture(IUiPainter &painter, const std::shared_ptr<const Picture> &picture) {
        if (auto *recording = dynamic_cast<RecordingPainter *>(&painter)) {
            recording->call(picture);
        } else if (picture != nullptr) {
            picture->replay(painter);
        }
    }

} // namespace engine::ui
