#include "ui/render/font_library.h"

#include "ui/math/math_font.h"
#include "ui/text_wrap.h"

#include <engine/builtin_ids.h>
#include <engine/log.h>

#include <utility>

namespace engine::ui {
namespace {

[[nodiscard]] glm::vec2 rough_extent(std::string_view text, float size) noexcept {
    return {static_cast<float>(text.size()) * size * 0.5f, size};
}

}

FontLibrary::FontLibrary(int atlas_page_size) : atlas_(atlas_page_size) {}

FontLibrary::~FontLibrary() = default;

bool FontLibrary::add_font(AssetId id, const Font& font) {
    if (font.bytes.empty()) {
        return false;
    }
    if (id == builtin::font_math) {
        if (math_font_ != nullptr) {
            return true;
        }
        auto loaded = math::MathFont::load(font.bytes);
        if (!loaded) {
            log::error("builtin math font failed to load as an OpenType MATH font");
            return false;
        }
        math_font_ = std::make_unique<math::MathFont>(std::move(*loaded));
        return true;
    }
    if (has_font(id)) {
        return true;
    }
    auto face = FontFace::load(font.bytes);
    if (!face) {
        return false;
    }
    // Faces are never removed, so the index is a stable atlas id.
    const auto atlas_id = static_cast<std::uint32_t>(faces_.size());
    faces_.push_back(std::make_unique<Entry>(Entry{id, atlas_id, std::move(*face)}));
    return true;
}

FontLibrary::Entry* FontLibrary::find(AssetId id) noexcept {
    for (const auto& entry : faces_) {
        if (entry->id == id) {
            return entry.get();
        }
    }
    return nullptr;
}

bool FontLibrary::has_font(AssetId id) const noexcept {
    for (const auto& entry : faces_) {
        if (entry->id == id) {
            return true;
        }
    }
    return false;
}

FontLibrary::Entry* FontLibrary::resolve(AssetId id) noexcept {
    Entry* entry = find(id);
    if (entry == nullptr && id != builtin::font_ui) {
        entry = find(builtin::font_ui);
    }
    return entry;
}

FontFace* FontLibrary::face(AssetId id) noexcept {
    Entry* entry = resolve(id);
    return entry != nullptr ? &entry->face : nullptr;
}

TextFontMetrics FontLibrary::font_metrics(AssetId font, float size) {
    if (FontFace* f = face(font)) {
        return text_metrics(*f, size);
    }
    return TextFontMetrics{size * 0.8f, size * 0.2f, size};
}

glm::vec2 FontLibrary::measure_text(std::string_view text, AssetId font, float size) {
    if (FontFace* f = face(font)) {
        return text_extent(*f, text, size);
    }
    return rough_extent(text, size);
}

TextBlock FontLibrary::break_lines(std::string_view text, AssetId font, float size, float max_width) {
    if (FontFace* f = face(font)) {
        return wrap_text(*f, text, size, max_width);
    }
    TextBlock block;
    block.line_height = size;
    block.lines = break_text_lines(
            text, max_width, [size](std::string_view slice) { return rough_extent(slice, size).x; });
    return block;
}

float FontLibrary::layout_text(std::string_view text, AssetId font, float size, glm::vec2 position,
        UiAlign horizontal, UiAlign vertical, float raster_scale, std::vector<GlyphQuad>& out) {
    Entry* entry = resolve(font);
    if (entry == nullptr) {
        return position.x;
    }
    return layout_glyphs(entry->face, entry->atlas_id, atlas_, text, size, position, horizontal, vertical,
            raster_scale, out);
}

}
