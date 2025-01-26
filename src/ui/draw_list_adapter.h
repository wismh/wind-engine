#pragma once

#include "ui/painter.h"

#include <engine/ui/draw_list.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <string_view>

namespace engine::ui {

class DrawListAdapter final : public IDrawList {
public:
    DrawListAdapter(IUiPainter& painter, glm::vec2 origin, float scale)
        : painter_(painter), origin_(origin), scale_(scale) {}

    void line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) override {
        painter_.draw_line(map(from), map(to), color, width * scale_);
    }

    void fill_rect(const render::Rect& rect, glm::vec4 color, float radius) override {
        painter_.fill_rounded_rect(map(rect), radius * scale_, color);
    }

    void stroke_rect(const render::Rect& rect, glm::vec4 color, float width, float radius) override {
        painter_.stroke_rounded_rect(map(rect), radius * scale_, width * scale_, color);
    }

    void set_font(AssetId font, float size) override {
        painter_.set_font(font, size * scale_);
    }

    void text(std::string_view value, glm::vec2 position, glm::vec4 color) override {
        painter_.fill_text(value, map(position), color, UiAlign::Start, UiAlign::Start);
    }

    void image(AssetId texture, const render::Rect& rect) override {
        painter_.image(texture, map(rect));
    }

private:
    [[nodiscard]] glm::vec2 map(glm::vec2 local) const {
        return origin_ + local * scale_;
    }

    [[nodiscard]] render::Rect map(const render::Rect& local) const {
        return render::Rect{
                origin_.x + local.x * scale_,
                origin_.y + local.y * scale_,
                local.w * scale_,
                local.h * scale_,
        };
    }

    IUiPainter& painter_;
    glm::vec2 origin_{};
    float scale_ = 1.0f;
};

}
