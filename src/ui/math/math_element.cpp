#include "ui/math/math_element.h"

#include "ui/math/math_paint.h"
#include "ui/math/math_parser.h"

#include <engine/log.h>

#include <memory>
#include <string>

namespace engine::ui::math {
namespace {

struct ElementMathCache {
    std::string formula;
    float font_size = 0.0f;
    bool display = false;
    const MathFont* font = nullptr;
    MathLayout layout;
};

}

const MathLayout& element_layout(const Element& element, const MathFont& font, float font_size) {
    if (const auto* cache = static_cast<const ElementMathCache*>(element.math_cache.get())) {
        if (cache->formula == element.text && cache->font_size == font_size && cache->display == element.math_display &&
                cache->font == &font) {
            return cache->layout;
        }
    }

    const ParseResult parsed = parse_formula(element.text);
    if (!parsed.ok()) {
        log::warn("math formula '" + element.text + "' has " + std::to_string(parsed.errors.size()) +
                " error(s), first: " + describe(parsed.errors.front()));
    }
    auto fresh = std::make_shared<ElementMathCache>();
    fresh->formula = element.text;
    fresh->font_size = font_size;
    fresh->display = element.math_display;
    fresh->font = &font;
    fresh->layout = layout_formula(parsed.root, font, LayoutOptions{font_size, element.math_display});
    const MathLayout& layout = fresh->layout;
    // Replace, never mutate: the old entry may be shared with Elements cloned from the same template.
    element.math_cache = std::move(fresh);
    return layout;
}

void paint_element(IUiPainter& painter, const MathFont& font, const Element& element, float font_size,
        const render::Rect& content, float ui_scale, UiAlign horizontal, UiAlign vertical, glm::vec4 color) {
    const MathLayout& layout = element_layout(element, font, font_size);
    const float width = layout.width * ui_scale;
    const float height = layout.height() * ui_scale;
    float x = content.x;
    if (horizontal == UiAlign::Center) {
        x += (content.w - width) * 0.5f;
    } else if (horizontal == UiAlign::End) {
        x += content.w - width;
    }
    float y = content.y;
    if (vertical == UiAlign::Center) {
        y += (content.h - height) * 0.5f;
    } else if (vertical == UiAlign::End) {
        y += content.h - height;
    }
    paint_layout(painter, font, layout, {x, y}, ui_scale, color);
}

}
