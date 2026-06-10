#include "ui/render/ramp_atlas.h"

#include "ui/render/fragment.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>

namespace engine::ui {
namespace {

[[nodiscard]] std::int64_t micro(float value) noexcept {
    return std::llround(static_cast<double>(value) * 1e6);
}

[[nodiscard]] glm::vec4 premultiply(glm::vec4 color) noexcept {
    const glm::vec4 c = glm::clamp(color, 0.0f, 1.0f);
    return {c.r * c.a, c.g * c.a, c.b * c.a, c.a};
}

// `percent` (0..100) against stops whose percents are resolved.
[[nodiscard]] glm::vec4 sample_stops(const std::vector<GradientStop>& stops, float percent) {
    if (percent <= stops.front().percent.value_or(0.0f)) {
        return stops.front().color;
    }
    if (percent >= stops.back().percent.value_or(100.0f)) {
        return stops.back().color;
    }
    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        const float p0 = stops[i].percent.value_or(0.0f);
        const float p1 = stops[i + 1].percent.value_or(100.0f);
        if (percent > p1) {
            continue;
        }
        const float t = p1 > p0 ? (percent - p0) / (p1 - p0) : 0.0f;
        return glm::mix(stops[i].color, stops[i + 1].color, t);
    }
    return stops.back().color;
}

[[nodiscard]] std::uint8_t to_byte(float value) noexcept {
    return static_cast<std::uint8_t>(std::lround(static_cast<double>(std::clamp(value, 0.0f, 1.0f)) * 255.0));
}

}

void RampAtlas::key_of(const Gradient& gradient) {
    key_.clear();
    const auto push_color = [this](glm::vec4 color) {
        key_.push_back(micro(color.r));
        key_.push_back(micro(color.g));
        key_.push_back(micro(color.b));
        key_.push_back(micro(color.a));
    };
    if (gradient.kind == GradientKind::Conic) {
        key_.push_back(1);
        for (const GradientStop& stop : gradient.stops) {
            push_color(stop.color);
            key_.push_back(micro(stop.percent.value_or(-1.0f)));
        }
    } else {
        key_.push_back(0);
        push_color(gradient.stops.front().color);
        push_color(gradient.stops.back().color);
    }
}

std::size_t RampAtlas::take_slot() {
    if (next_free_ < rows_.size()) {
        return next_free_++;
    }
    std::size_t victim = rows_.size();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].used != serial_ && (victim == rows_.size() || rows_[i].used < rows_[victim].used)) {
            victim = i;
        }
    }
    if (victim != rows_.size()) {
        const Row& old = rows_[victim];
        const auto it = lookup_.find(old.hash);
        if (old.filled && it != lookup_.end() && it->second == victim) {
            lookup_.erase(it);
        }
        return victim;
    }
    pages_.emplace_back(kRampWidth, kRowsPerPage, 4);
    rows_.resize(rows_.size() + kRowsPerPage);
    return next_free_++;
}

void RampAtlas::write_row(std::size_t slot, const Gradient& gradient) {
    AtlasPage& page = pages_[slot / kRowsPerPage];
    const int y = static_cast<int>(slot % kRowsPerPage);
    std::uint8_t* texels = page.row(y);
    const glm::vec4 first = premultiply(gradient.stops.front().color);
    const glm::vec4 last = premultiply(gradient.stops.back().color);
    for (int i = 0; i < kRampWidth; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kRampWidth - 1);
        const glm::vec4 color = gradient.kind == GradientKind::Conic
                                        ? premultiply(sample_stops(gradient.stops, t * 100.0f))
                                        : glm::mix(first, last, t);
        texels[i * 4 + 0] = to_byte(color.r);
        texels[i * 4 + 1] = to_byte(color.g);
        texels[i * 4 + 2] = to_byte(color.b);
        texels[i * 4 + 3] = to_byte(color.a);
    }
    page.mark_dirty({0, y, kRampWidth, y + 1}, ++version_);
}

RampRow RampAtlas::row(const Gradient& gradient) {
    key_of(gradient);
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::int64_t value : key_) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    }
    std::size_t slot = 0;
    const auto found = lookup_.find(hash);
    if (found != lookup_.end() && rows_[found->second].key == key_) {
        slot = found->second;
    } else {
        slot = take_slot();
        write_row(slot, gradient);
        Row& row = rows_[slot];
        row.key = key_;
        row.hash = hash;
        row.filled = true;
        lookup_[hash] = slot;
    }
    rows_[slot].used = serial_;
    const auto y = static_cast<float>(slot % kRowsPerPage);
    return {static_cast<std::uint32_t>(slot / kRowsPerPage), (y + 0.5f) / static_cast<float>(kRowsPerPage)};
}

}
