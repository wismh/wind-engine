// The engine's second stb_truetype instance, private to this file like the one in ui/math/math_font.cpp
// (STBTT_STATIC), so the two never collide. Every stbtt call of the UI text renderer goes through FontFace.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4456)
#pragma warning(disable : 4505)
#pragma warning(disable : 4701)
#pragma warning(disable : 4702)
#endif

#include "resources/stb_truetype.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "ui/render/font_face.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::ui {
namespace {

[[nodiscard]] std::uint64_t pair_key(int a, int b) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) | static_cast<std::uint32_t>(b);
}

}

struct FontFace::Impl {
    std::vector<std::uint8_t> bytes;
    stbtt_fontinfo info{};
    float ascender = 0.0f;
    float descender = 0.0f;
    float line_height = 0.0f;
    std::unordered_map<std::uint32_t, int> glyph_of_codepoint;
    // Key: glyph << 32 | isize. Node-based, so the references glyph_metrics hands out stay valid.
    std::unordered_map<std::uint64_t, GlyphMetrics> metrics;
    std::unordered_map<std::uint64_t, int> kerning;
};

FontFace::FontFace(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FontFace::FontFace(FontFace&&) noexcept = default;
FontFace& FontFace::operator=(FontFace&&) noexcept = default;
FontFace::~FontFace() = default;

std::optional<FontFace> FontFace::load(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return std::nullopt;
    }
    auto impl = std::make_unique<Impl>();
    impl->bytes.assign(bytes.begin(), bytes.end());
    const unsigned char* data = impl->bytes.data();
    // fons__tt_loadFont: font index 0.
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || stbtt_InitFont(&impl->info, data, offset) == 0) {
        return std::nullopt;
    }
    // fonsAddFontMem: the line gap joins the ascent, and the metrics are normalized by the full height.
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&impl->info, &ascent, &descent, &line_gap);
    ascent += line_gap;
    const int height = ascent - descent;
    if (height <= 0) {
        return std::nullopt;
    }
    impl->ascender = static_cast<float>(ascent) / static_cast<float>(height);
    impl->descender = static_cast<float>(descent) / static_cast<float>(height);
    impl->line_height = impl->ascender - impl->descender;
    return FontFace(std::move(impl));
}

float FontFace::ascender() const noexcept {
    return impl_->ascender;
}

float FontFace::descender() const noexcept {
    return impl_->descender;
}

float FontFace::line_height() const noexcept {
    return impl_->line_height;
}

float FontFace::scale(int isize) const {
    // fons__getGlyph: `float size = isize / 10.0f`, then fons__tt_getPixelHeightScale, which is the em mapping.
    return stbtt_ScaleForMappingEmToPixels(&impl_->info, static_cast<float>(isize) / 10.0f);
}

int FontFace::glyph_index(std::uint32_t codepoint) {
    const auto it = impl_->glyph_of_codepoint.find(codepoint);
    if (it != impl_->glyph_of_codepoint.end()) {
        return it->second;
    }
    const int glyph = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(codepoint));
    impl_->glyph_of_codepoint.emplace(codepoint, glyph);
    return glyph;
}

const GlyphMetrics& FontFace::glyph_metrics(int glyph, int isize) {
    const std::uint64_t key = pair_key(glyph, isize);
    const auto it = impl_->metrics.find(key);
    if (it != impl_->metrics.end()) {
        return it->second;
    }
    // fons__getGlyph + fons__tt_buildGlyphBitmap, with the same float expression order.
    const float scale = this->scale(isize);
    int advance = 0;
    int lsb = 0;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetGlyphHMetrics(&impl_->info, glyph, &advance, &lsb);
    stbtt_GetGlyphBitmapBox(&impl_->info, glyph, scale, scale, &x0, &y0, &x1, &y1);
    GlyphMetrics metrics;
    metrics.glyph = glyph;
    metrics.xadv = static_cast<std::int16_t>(scale * static_cast<float>(advance) * 10.0f);
    metrics.box_x0 = static_cast<std::int16_t>(x0);
    metrics.box_y0 = static_cast<std::int16_t>(y0);
    metrics.box_x1 = static_cast<std::int16_t>(x1);
    metrics.box_y1 = static_cast<std::int16_t>(y1);
    return impl_->metrics.emplace(key, metrics).first->second;
}

int FontFace::kern_advance(int left_glyph, int right_glyph) {
    const std::uint64_t key = pair_key(left_glyph, right_glyph);
    const auto it = impl_->kerning.find(key);
    if (it != impl_->kerning.end()) {
        return it->second;
    }
    const int kern = stbtt_GetGlyphKernAdvance(&impl_->info, left_glyph, right_glyph);
    impl_->kerning.emplace(key, kern);
    return kern;
}

void FontFace::rasterize(int glyph, int isize, std::uint8_t* out, int width, int height, int stride) const {
    if (width <= 0 || height <= 0) {
        return;
    }
    const float scale = this->scale(isize);
    stbtt_MakeGlyphBitmap(&impl_->info, out, width, height, stride, scale, scale, glyph);
}

}
