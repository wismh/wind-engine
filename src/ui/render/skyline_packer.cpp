#include "ui/render/skyline_packer.h"

#include <algorithm>

namespace engine::ui {

SkylinePacker::SkylinePacker(int width, int height) : width_(width), height_(height) {
    spans_.push_back({0, 0, width});
}

int SkylinePacker::fits(std::size_t index, int w, int h) const {
    const int x = spans_[index].x;
    int y = spans_[index].y;
    if (x + w > width_) {
        return -1;
    }
    int space_left = w;
    while (space_left > 0) {
        if (index == spans_.size()) {
            return -1;
        }
        y = std::max(y, spans_[index].y);
        if (y + h > height_) {
            return -1;
        }
        space_left -= spans_[index].width;
        ++index;
    }
    return y;
}

void SkylinePacker::add_level(std::size_t index, int x, int y, int w, int h) {
    const auto at = [this](std::size_t i) { return spans_.begin() + static_cast<std::ptrdiff_t>(i); };
    spans_.insert(at(index), Span{x, y + h, w});
    // Shrink or drop the spans the new one now shadows.
    for (std::size_t i = index + 1; i < spans_.size();) {
        const Span& previous = spans_[i - 1];
        if (spans_[i].x >= previous.x + previous.width) {
            break;
        }
        const int shrink = previous.x + previous.width - spans_[i].x;
        spans_[i].x += shrink;
        spans_[i].width -= shrink;
        if (spans_[i].width > 0) {
            break;
        }
        spans_.erase(at(i));
    }
    // Merge neighbours at the same height.
    for (std::size_t i = 0; i + 1 < spans_.size();) {
        if (spans_[i].y == spans_[i + 1].y) {
            spans_[i].width += spans_[i + 1].width;
            spans_.erase(at(i + 1));
        } else {
            ++i;
        }
    }
}

std::optional<glm::ivec2> SkylinePacker::add(int w, int h) {
    if (w <= 0 || h <= 0) {
        return std::nullopt;
    }
    int best_h = height_;
    int best_w = width_;
    std::optional<std::size_t> best;
    glm::ivec2 best_pos{};
    for (std::size_t i = 0; i < spans_.size(); ++i) {
        const int y = fits(i, w, h);
        if (y == -1) {
            continue;
        }
        if (y + h < best_h || (y + h == best_h && spans_[i].width < best_w)) {
            best = i;
            best_w = spans_[i].width;
            best_h = y + h;
            best_pos = {spans_[i].x, y};
        }
    }
    if (!best) {
        return std::nullopt;
    }
    add_level(*best, best_pos.x, best_pos.y, w, h);
    return best_pos;
}

}
