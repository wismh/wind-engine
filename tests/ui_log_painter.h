#pragma once

#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/ui/document.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ui_test {

    // Logs every IUiPainter call with the state it ran under: the effective clip (the intersection of the active
    // scissors, in the painter's own space; no transform or view is applied to it, so tests that read `clip` keep to
    // untransformed trees) and the global alpha. `measure_text` is half a character per size unit wide, one size tall.
    class LogPainter final : public engine::ui::IUiPainter {
    public:
        struct Call {
            std::string op;
            engine::render::Rect rect{};
            engine::render::Rect clip{};
            bool clipped = false;
            float opacity = 1.0f;
            glm::vec4 color{};
            std::string text;
            float width = 0.0f;
        };

        std::vector<Call> calls;

        [[nodiscard]] int count(std::string_view op) const {
            return static_cast<int>(std::ranges::count_if(calls, [&](const Call &call) { return call.op == op; }));
        }

        [[nodiscard]] std::vector<const Call *> all(std::string_view op) const {
            std::vector<const Call *> out;
            for (const Call &call: calls) {
                if (call.op == op) {
                    out.push_back(&call);
                }
            }
            return out;
        }

        // The first call of `op` whose color is `color` (fills and strokes are told apart by color in the fixtures).
        [[nodiscard]] const Call *with_color(std::string_view op, glm::vec4 color) const {
            for (const Call &call: calls) {
                if (call.op == op && call.color == color) {
                    return &call;
                }
            }
            return nullptr;
        }

        void save() override {
            log("save");
            stack_.push_back(state_);
        }
        void restore() override {
            log("restore");
            if (!stack_.empty()) {
                state_ = stack_.back();
                stack_.pop_back();
            }
        }
        void scissor(const engine::render::Rect &rect) override {
            engine::render::Rect applied = rect;
            if (state_.clipped) {
                const float x0 = std::max(rect.x, state_.clip.x);
                const float y0 = std::max(rect.y, state_.clip.y);
                const float x1 = std::min(rect.x + rect.w, state_.clip.x + state_.clip.w);
                const float y1 = std::min(rect.y + rect.h, state_.clip.y + state_.clip.h);
                applied = {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
            }
            state_.clip = applied;
            state_.clipped = true;
            Call &call = log("scissor");
            call.rect = rect;
        }
        void apply_transform(glm::vec2, float, float) override { log("transform"); }
        void apply_view(glm::vec2, glm::vec2, float) override { log("view"); }
        void set_opacity(float opacity) override {
            state_.opacity = opacity;
            log("opacity").width = opacity;
        }
        void fill_rounded_rect(const engine::render::Rect &rect, float, glm::vec4 color) override {
            Call &call = log("fill");
            call.rect = rect;
            call.color = color;
        }
        void fill_rounded_rect_gradient(const engine::render::Rect &rect, float,
                                        const engine::ui::Gradient &) override {
            log("gradient").rect = rect;
        }
        void stroke_rounded_rect(const engine::render::Rect &rect, float, float width, glm::vec4 color) override {
            Call &call = log("stroke");
            call.rect = rect;
            call.color = color;
            call.width = width;
        }
        void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override { log("line"); }
        void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override { log("arc"); }
        void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override { log("path"); }
        void set_font(engine::AssetId, float) override { log("font"); }
        void fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, engine::ui::UiAlign,
                       engine::ui::UiAlign) override {
            Call &call = log("text");
            call.text = std::string(text);
            call.rect = {position.x, position.y, 0.0f, 0.0f};
            call.color = color;
        }
        void image(engine::AssetId, const engine::render::Rect &rect) override { log("image").rect = rect; }
        void image_repeat(engine::AssetId, const engine::render::Rect &rect) override {
            log("image_repeat").rect = rect;
        }
        void image_nine_slice(engine::AssetId, const engine::render::Rect &rect,
                              const engine::ui::BoxInsets &) override {
            log("nine_slice").rect = rect;
        }
        glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
            ++measure_calls;
            return {static_cast<float>(text.size()) * size * 0.5f, size};
        }

        // Times the layout asked this painter to measure a string.
        int measure_calls = 0;

    private:
        struct State {
            engine::render::Rect clip{};
            bool clipped = false;
            float opacity = 1.0f;
        };

        Call &log(std::string_view op) {
            Call &call = calls.emplace_back();
            call.op = std::string(op);
            call.clip = state_.clip;
            call.clipped = state_.clipped;
            call.opacity = state_.opacity;
            return call;
        }

        State state_;
        std::vector<State> stack_;
    };

    // `rect` clipped to `clip` (nothing: width and height 0).
    [[nodiscard]] inline engine::render::Rect clipped_to(const engine::render::Rect &rect,
                                                         const engine::render::Rect &clip) {
        const float x0 = std::max(rect.x, clip.x);
        const float y0 = std::max(rect.y, clip.y);
        const float x1 = std::min(rect.x + rect.w, clip.x + clip.w);
        const float y1 = std::min(rect.y + rect.h, clip.y + clip.h);
        return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }

    [[nodiscard]] inline bool same_rect(const engine::render::Rect &a, const engine::render::Rect &b,
                                        float eps = 0.01f) {
        const auto close = [eps](float x, float y) { return x >= y - eps && x <= y + eps; };
        return close(a.x, b.x) && close(a.y, b.y) && close(a.w, b.w) && close(a.h, b.h);
    }

} // namespace ui_test
