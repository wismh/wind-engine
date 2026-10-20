#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace engine::ui {

// One glyph of a face at one quantized size, as fontstash's FONSglyph records it. Sizes are in tenths of a pixel
// (`isize`, fontstash's `(short)(size * 10.0f)`); everything else is whole pixels.
struct GlyphMetrics {
    int glyph = 0;
    // Advance in tenths of a pixel, truncated: `(short)(scale * advance * 10.0f)`. The pen moves by
    // `(int)(xadv / 10.0f + 0.5f)` whole pixels.
    std::int16_t xadv = 0;
    // stbtt_GetGlyphBitmapBox at the size's scale: the ink box relative to the pen on the baseline (y down).
    std::int16_t box_x0 = 0;
    std::int16_t box_y0 = 0;
    std::int16_t box_x1 = 0;
    std::int16_t box_y1 = 0;

    [[nodiscard]] int bitmap_width() const noexcept { return box_x1 - box_x0; }
    [[nodiscard]] int bitmap_height() const noexcept { return box_y1 - box_y0; }
};

// A TrueType/OpenType (glyf) face parsed with the vendored stb_truetype, and the per-face caches text layout hits on
// every call: cmap lookups, per-size glyph metrics, and kerning pairs. All numbers follow fontstash so layout keeps
// the widths it had: the em scale (`stbtt_ScaleForMappingEmToPixels`), the
// normalized vertical metrics, and the truncated tenth-pixel advances. Main thread only; the caches mutate on lookup.
class FontFace {
public:
    // Copies `bytes`. Empty when stb_truetype rejects them.
    [[nodiscard]] static std::optional<FontFace> load(std::span<const std::uint8_t> bytes);

    FontFace(FontFace&&) noexcept;
    FontFace& operator=(FontFace&&) noexcept;
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;
    ~FontFace();

    // fontstash's normalized vertical metrics: ascent includes the line gap, and the three are fractions of
    // (ascent + lineGap - descent), so `line_height() == ascender() - descender()` and a size's pixel value is
    // `ascender() * isize / 10.0f`. `descender()` is negative.
    [[nodiscard]] float ascender() const noexcept;
    [[nodiscard]] float descender() const noexcept;
    [[nodiscard]] float line_height() const noexcept;

    // Pixels per font unit at `isize` tenths of a pixel (the em, not the ascent-to-descent height).
    [[nodiscard]] float scale(int isize) const;
    // 0 (.notdef) for a code point the cmap does not map; fontstash draws .notdef then, and so does this.
    [[nodiscard]] int glyph_index(std::uint32_t codepoint);
    [[nodiscard]] const GlyphMetrics& glyph_metrics(int glyph, int isize);
    // Kerning between two glyphs in font units (kern table or GPOS pair adjustment, as stb_truetype reads them).
    [[nodiscard]] int kern_advance(int left_glyph, int right_glyph);
    // Rasterizes `glyph` at `isize` with stbtt_MakeGlyphBitmap into `width` x `height` pixels starting at `out`, rows
    // `stride` bytes apart; the box is `glyph_metrics(glyph, isize)`'s bitmap size.
    void rasterize(int glyph, int isize, std::uint8_t* out, int width, int height, int stride) const;

private:
    struct Impl;
    explicit FontFace(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}
