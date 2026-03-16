#pragma once

#include "ui/render/atlas_page.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::ui {

class FontFace;
struct GlyphMetrics;

// Where a rasterized glyph lives: its page and the packed cell. The cell is fontstash's glyph rect: the bitmap with
// a 2 px border of zeros on every side (fontstash's `pad = blur + 2` at blur 0). A quad samples the cell inset by
// one pixel, so bilinear filtering at its edge reads zeros, never a neighbour.
struct AtlasGlyph {
    std::uint32_t page = 0;
    AtlasRect cell;
};

// The CPU side of the UI glyph atlas: R8 pages, glyphs rasterized with stb_truetype exactly as fontstash rasterizes
// them (stbtt_MakeGlyphBitmap at the em scale of the quantized size, no blur), packed with a skyline packer. A glyph
// is cached by face, glyph index, and size in tenths of a pixel, and never moves once placed. When the newest page
// is full a new page opens (NanoVG instead reset its single atlas into a bigger texture); pages are not evicted.
// One instance serves every window; it holds no GPU handle. Main thread only.
class GlyphAtlas {
public:
    static constexpr int kDefaultPageSize = 1024;
    // fontstash's padding around a glyph bitmap at blur 0.
    static constexpr int kPadding = 2;

    explicit GlyphAtlas(int page_size = kDefaultPageSize);

    // The glyph's cell, rasterizing and packing it on first use. `face_id` tells faces apart in the cache.
    [[nodiscard]] const AtlasGlyph& glyph(FontFace& face, std::uint32_t face_id, const GlyphMetrics& metrics,
            int isize);

    [[nodiscard]] std::size_t page_count() const noexcept { return pages_.size(); }
    [[nodiscard]] const AtlasPage& page(std::size_t index) const { return pages_[index]; }
    // Bumped by every write to any page; equal versions mean nothing changed.
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    [[nodiscard]] std::size_t glyph_count() const noexcept { return glyphs_.size(); }
    [[nodiscard]] int page_size() const noexcept { return page_size_; }

private:
    [[nodiscard]] AtlasGlyph place(int w, int h);

    int page_size_ = kDefaultPageSize;
    std::vector<AtlasPage> pages_;
    std::uint64_t version_ = 0;
    // Key: face_id << 48 | glyph << 16 | isize. Node-based, so handed-out references stay valid.
    std::unordered_map<std::uint64_t, AtlasGlyph> glyphs_;
};

}
