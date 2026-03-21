#pragma once

#include <optional>
#include <vector>

#include <glm/vec2.hpp>

namespace engine::ui {

// Bottom-left skyline rectangle packer (Jukka Jylanki's skyline bin packer, as fontstash's atlas runs it): a
// rectangle goes where its bottom edge ends lowest, ties to the narrower skyline span. Rectangles never overlap and
// never leave the `width` x `height` area. No removal: a full packer is replaced, not compacted.
class SkylinePacker {
public:
    SkylinePacker(int width, int height);

    // Top-left corner of a free `w` x `h` rectangle, now taken; empty when it does not fit anywhere.
    [[nodiscard]] std::optional<glm::ivec2> add(int w, int h);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    struct Span {
        int x = 0;
        int y = 0;
        int width = 0;
    };

    // The lowest y a `w` x `h` rectangle can sit at with its left edge on span `index`, or -1.
    [[nodiscard]] int fits(std::size_t index, int w, int h) const;
    void add_level(std::size_t index, int x, int y, int w, int h);

    int width_ = 0;
    int height_ = 0;
    std::vector<Span> spans_;
};

}
