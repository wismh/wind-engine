#pragma once

#include "ui/painter.h"
#include "ui/render/affine.h"
#include "ui/render/atlas_page.h"
#include "ui/render/path_raster.h"

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine::ui {

// A rasterized path in the mask atlas. `cell` is the whole packed rect (a `kPadding` border of zeros included) and
// `origin` is where the cell's top-left texel corner sits in raster space: the path's points mapped by the linear part
// the mask was made for, plus its phase, with no translation.
struct AtlasMask {
    std::uint32_t page = 0;
    AtlasRect cell;
    glm::vec2 origin{};
};

// R8 pages of fill_path coverage, cached by path content, the transform's linear part (rotation and scale, quantized
// to 1/4096), and the sub-pixel phase of its translation (quantized to 1/4 px). A static formula is rasterized once;
// it is redrawn from the cache at any integer offset. Pages are evicted whole, least recently used first, once
// `kMaxPages` exist, but never a page the current list (begin_list) drew from. CPU only; a device catches up with
// page versions like the glyph atlas. Main thread only.
class MaskAtlas {
public:
    static constexpr int kDefaultPageSize = 1024;
    // Rasterizer bounds already hold the fringe: one texel of zero around them for bilinear sampling, one of slack.
    static constexpr int kPadding = 2;
    static constexpr std::size_t kMaxPages = 4;
    static constexpr float kPhaseSteps = 4.0f;

    explicit MaskAtlas(int page_size = kDefaultPageSize);

    // A new draw list starts: what it uses from now on is protected from eviction until the next call.
    void begin_list() noexcept { ++serial_; }

    // The mask of `path` mapped by `linear` (its translation must be zero) and shifted by `phase` (each component a
    // multiple of 1 / kPhaseSteps in [0, 1)); nullptr when the path covers nothing. `tolerance` is the flattening
    // tolerance (PathRasterizer::flatten).
    [[nodiscard]] const AtlasMask* mask(std::span<const PathSegment> path, const Affine2& linear, glm::vec2 phase,
            float tolerance = 0.25f);

    [[nodiscard]] std::size_t page_count() const noexcept { return pages_.size(); }
    [[nodiscard]] const AtlasPage& page(std::size_t index) const { return pages_[index]; }
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    [[nodiscard]] std::size_t mask_count() const noexcept { return entries_.size(); }
    // Masks rasterized so far (cache misses), for tests and the profiler.
    [[nodiscard]] std::uint64_t rasterized_count() const noexcept { return rasterized_; }

private:
    struct Entry {
        std::vector<PathSegment> path;
        std::int32_t linear[4] = {};
        std::int32_t phase[2] = {};
        AtlasMask mask;
    };

    [[nodiscard]] AtlasMask place(int w, int h);
    void evict_page(std::uint32_t page);

    int page_size_ = kDefaultPageSize;
    std::vector<AtlasPage> pages_;
    std::vector<std::uint64_t> page_used_;
    std::uint32_t current_page_ = 0;
    std::uint64_t serial_ = 1;
    std::uint64_t version_ = 0;
    std::uint64_t rasterized_ = 0;
    std::unordered_map<std::uint64_t, Entry> entries_;
    PathRasterizer rasterizer_;
};

}
