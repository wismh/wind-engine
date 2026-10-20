#pragma once

// docs/tech/modules/UI.md

#include <engine/ui/path.h>

#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace engine::ui {

// One filled or stroked contour of an icon, in viewBox units. `current_color` takes the element's CSS `color`
// at paint; `color.a` is still the icon's own opacity. A literal `color` is already the color to draw.
struct SvgShape {
    enum class Paint : std::uint8_t {
        Fill,
        Stroke,
    };

    std::vector<PathSegment> segments;
    Paint paint = Paint::Fill;
    // Stroke width in viewBox units. Unused for a fill.
    float width = 0.0f;
    bool current_color = false;
    glm::vec4 color{0.0f, 0.0f, 0.0f, 1.0f};
};

// A parsed icon. `width` and `height` are the viewBox size; `min_x` and `min_y` are its origin.
struct SvgPicture {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::vector<SvgShape> shapes;
};

}
