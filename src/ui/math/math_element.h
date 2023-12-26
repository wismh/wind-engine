#pragma once

#include "ui/math/math_font.h"
#include "ui/math/math_layout.h"
#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/ui/document.h>

#include <glm/vec4.hpp>

namespace engine::ui::math {

// Everything the UI needs of `<Math>`: parse + layout of the element's formula (`Element::text`), cached on
// the element, and painting it into the element's content box.

// The formula laid out at `font_size` (design px) in `font`. Cached in `Element::math_cache` and rebuilt only
// when the text, size, display flag or font changes, so a formula that stays put costs one string compare per
// query. A formula with parse errors still lays out (the parser recovers: an unknown command shows up as its own
// text) and is logged once per rebuild, not once per frame.
[[nodiscard]] const MathLayout& element_layout(const Element& element, const MathFont& font, float font_size);

// Draws the element's formula inside `content` (real pixels), aligned by `horizontal` / `vertical` the way
// text is (`text-align` / `align-items`). `font_size` is the design size the layout was built at; `ui_scale`
// maps design units to real pixels, so a canvas rescale never invalidates the cached layout.
void paint_element(IUiPainter& painter, const MathFont& font, const Element& element, float font_size,
        const render::Rect& content, float ui_scale, UiAlign horizontal, UiAlign vertical, glm::vec4 color);

}
