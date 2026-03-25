#include "ui/render/glyph_atlas.h"

#include "ui/render/font_face.h"

#include <algorithm>
#include <bit>

namespace engine::ui {

GlyphAtlas::GlyphAtlas(int page_size) : page_size_(std::max(page_size, 16)) {}

AtlasGlyph GlyphAtlas::place(int w, int h) {
    if (!pages_.empty()) {
        if (const auto at = pages_.back().packer().add(w, h)) {
            return {static_cast<std::uint32_t>(pages_.size() - 1), {at->x, at->y, at->x + w, at->y + h}};
        }
    }
    // A glyph larger than a page gets a page of its own size (a power of two), so huge text still draws.
    const int side = std::max(page_size_, static_cast<int>(std::bit_ceil(static_cast<unsigned>(std::max(w, h)))));
    pages_.emplace_back(side, side);
    const auto at = pages_.back().packer().add(w, h);
    return {static_cast<std::uint32_t>(pages_.size() - 1), {at->x, at->y, at->x + w, at->y + h}};
}

const AtlasGlyph& GlyphAtlas::glyph(FontFace& face, std::uint32_t face_id, const GlyphMetrics& metrics, int isize) {
    const std::uint64_t key = (static_cast<std::uint64_t>(face_id & 0xFFFFu) << 48) |
                              (static_cast<std::uint64_t>(static_cast<std::uint32_t>(metrics.glyph)) << 16) |
                              static_cast<std::uint16_t>(isize);
    const auto it = glyphs_.find(key);
    if (it != glyphs_.end()) {
        return it->second;
    }
    const int bitmap_w = metrics.bitmap_width();
    const int bitmap_h = metrics.bitmap_height();
    const AtlasGlyph placed = place(bitmap_w + kPadding * 2, bitmap_h + kPadding * 2);
    AtlasPage& page = pages_[placed.page];
    // A fresh cell is already zero, so the border fontstash clears after rasterizing is zero here too.
    face.rasterize(metrics.glyph, isize, page.row(placed.cell.y0 + kPadding) + placed.cell.x0 + kPadding, bitmap_w,
            bitmap_h, page.width());
    page.mark_dirty(placed.cell, ++version_);
    return glyphs_.emplace(key, placed).first->second;
}

}
