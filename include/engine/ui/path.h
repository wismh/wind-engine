#pragma once

#include <glm/vec2.hpp>

#include <cstdint>

namespace engine::ui {

// One segment of a path. A `Move` starts a new contour. A fill closes the previous contour at the next `Move`
// and at the end of the path (glyph outlines have no explicit close). A stroke closes only on `Close`; an open
// end gets a round cap. `Quad` curves through control point `c1`, `Cubic` through `c1` and `c2`. Coordinates
// are real pixels. `Close` ignores `p`, `c1`, and `c2`.
struct PathSegment {
    enum class Kind : std::uint8_t {
        Move,
        Line,
        Quad,
        Cubic,
        Close,
    };

    Kind kind = Kind::Move;
    glm::vec2 p{};
    glm::vec2 c1{};
    glm::vec2 c2{};
};

}
