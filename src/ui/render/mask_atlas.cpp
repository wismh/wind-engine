#include "ui/render/mask_atlas.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace engine::ui {
namespace {

struct Hasher {
    std::uint64_t value = 1469598103934665603ull;

    void add(std::uint32_t word) noexcept {
        for (int i = 0; i < 4; ++i) {
            value ^= (word >> (i * 8)) & 0xFFu;
            value *= 1099511628211ull;
        }
    }
    void add(float f) noexcept { add(std::bit_cast<std::uint32_t>(f)); }
    void add(glm::vec2 v) noexcept {
        add(v.x);
        add(v.y);
    }
};

[[nodiscard]] std::int32_t quantize(float value, float steps) noexcept {
    return static_cast<std::int32_t>(std::lround(static_cast<double>(value) * steps));
}

[[nodiscard]] bool same_path(std::span<const PathSegment> a, std::span<const PathSegment> b) noexcept {
    return std::ranges::equal(a, b, [](const PathSegment& x, const PathSegment& y) {
        return x.kind == y.kind && x.p == y.p && x.c1 == y.c1 && x.c2 == y.c2;
    });
}

}

MaskAtlas::MaskAtlas(int page_size) : page_size_(std::max(page_size, 16)) {}

void MaskAtlas::evict_page(std::uint32_t page) {
    std::erase_if(entries_, [page](const auto& item) { return item.second.mask.page == page; });
    pages_[page].reset_packing();
}

AtlasMask MaskAtlas::place(int w, int h) {
    if (!pages_.empty()) {
        if (const auto at = pages_[current_page_].packer().add(w, h)) {
            return {current_page_, {at->x, at->y, at->x + w, at->y + h}, {}};
        }
    }
    const int side = std::max(page_size_, static_cast<int>(std::bit_ceil(static_cast<unsigned>(std::max(w, h)))));
    if (pages_.size() >= kMaxPages) {
        // Least recently used page that the current list has not drawn from, of a size the mask fits.
        std::uint32_t victim = std::numeric_limits<std::uint32_t>::max();
        for (std::uint32_t i = 0; i < pages_.size(); ++i) {
            if (page_used_[i] == serial_ || pages_[i].width() < w || pages_[i].height() < h) {
                continue;
            }
            if (victim == std::numeric_limits<std::uint32_t>::max() || page_used_[i] < page_used_[victim]) {
                victim = i;
            }
        }
        if (victim != std::numeric_limits<std::uint32_t>::max()) {
            evict_page(victim);
            current_page_ = victim;
            const auto at = pages_[victim].packer().add(w, h);
            if (at) {
                return {victim, {at->x, at->y, at->x + w, at->y + h}, {}};
            }
        }
    }
    // Every page is in use by this list (or there are fewer than kMaxPages): a new page.
    pages_.emplace_back(side, side);
    page_used_.push_back(serial_);
    current_page_ = static_cast<std::uint32_t>(pages_.size() - 1);
    const auto at = pages_.back().packer().add(w, h);
    return {current_page_, {at->x, at->y, at->x + w, at->y + h}, {}};
}

const AtlasMask* MaskAtlas::mask(std::span<const PathSegment> path, const Affine2& linear, glm::vec2 phase,
        float tolerance) {
    return lookup(path, linear, phase, tolerance, -1);
}

const AtlasMask* MaskAtlas::stroke_mask(std::span<const PathSegment> path, const Affine2& linear, glm::vec2 phase,
        float width, float tolerance) {
    if (!(width > 0.0f)) {
        return nullptr;
    }
    constexpr float kWidthSteps = 64.0f;
    return lookup(path, linear, phase, tolerance, quantize(width, kWidthSteps));
}

const AtlasMask* MaskAtlas::lookup(std::span<const PathSegment> path, const Affine2& linear, glm::vec2 phase,
        float tolerance, std::int32_t width_q) {
    if (path.empty() || width_q == 0) {
        return nullptr;
    }
    constexpr float kLinearSteps = 4096.0f;
    constexpr float kWidthSteps = 64.0f;
    const std::int32_t linear_q[4] = {quantize(linear.a, kLinearSteps), quantize(linear.b, kLinearSteps),
            quantize(linear.c, kLinearSteps), quantize(linear.d, kLinearSteps)};
    const std::int32_t phase_q[2] = {quantize(phase.x, kPhaseSteps), quantize(phase.y, kPhaseSteps)};
    Hasher hasher;
    hasher.add(static_cast<std::uint32_t>(width_q));
    for (const PathSegment& segment : path) {
        hasher.add(static_cast<std::uint32_t>(segment.kind));
        hasher.add(segment.p);
        if (segment.kind == PathSegment::Kind::Quad || segment.kind == PathSegment::Kind::Cubic) {
            hasher.add(segment.c1);
        }
        if (segment.kind == PathSegment::Kind::Cubic) {
            hasher.add(segment.c2);
        }
    }
    for (const std::int32_t q : linear_q) {
        hasher.add(static_cast<std::uint32_t>(q));
    }
    hasher.add(static_cast<std::uint32_t>(phase_q[0]));
    hasher.add(static_cast<std::uint32_t>(phase_q[1]));

    const auto found = entries_.find(hasher.value);
    if (found != entries_.end()) {
        Entry& entry = found->second;
        if (entry.width_q == width_q && std::ranges::equal(entry.linear, linear_q) &&
                std::ranges::equal(entry.phase, phase_q) && same_path(entry.path, path)) {
            page_used_[entry.mask.page] = serial_;
            return &entry.mask;
        }
    }

    // The quantized values, not the exact ones, so every hit draws exactly what this miss rasterizes.
    const Affine2 to_raster{static_cast<float>(linear_q[0]) / kLinearSteps,
            static_cast<float>(linear_q[1]) / kLinearSteps, static_cast<float>(linear_q[2]) / kLinearSteps,
            static_cast<float>(linear_q[3]) / kLinearSteps, static_cast<float>(phase_q[0]) / kPhaseSteps,
            static_cast<float>(phase_q[1]) / kPhaseSteps};
    const float stroke_width = static_cast<float>(width_q) / kWidthSteps;
    const bool drawn = width_q < 0 ? rasterizer_.flatten(path, to_raster, tolerance)
                                   : rasterizer_.flatten_stroke(path, to_raster, stroke_width, tolerance);
    if (!drawn) {
        return nullptr;
    }
    const glm::vec2 origin = glm::floor(rasterizer_.bounds_min()) - static_cast<float>(kPadding);
    const glm::vec2 end = glm::ceil(rasterizer_.bounds_max()) + static_cast<float>(kPadding);
    const int w = static_cast<int>(end.x - origin.x);
    const int h = static_cast<int>(end.y - origin.y);
    AtlasMask placed = place(w, h);
    placed.origin = origin;
    AtlasPage& page = pages_[placed.page];
    rasterizer_.rasterize(-origin, w, h, page.row(placed.cell.y0) + placed.cell.x0, page.width());
    page.mark_dirty(placed.cell, ++version_);
    page_used_[placed.page] = serial_;
    ++rasterized_;

    Entry& entry = entries_[hasher.value];
    entry.path.assign(path.begin(), path.end());
    std::ranges::copy(linear_q, entry.linear);
    std::ranges::copy(phase_q, entry.phase);
    entry.width_q = width_q;
    entry.mask = placed;
    return &entry.mask;
}

}
