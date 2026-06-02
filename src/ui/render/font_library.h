#pragma once

#include "ui/painter.h"
#include "ui/render/font_face.h"
#include "ui/render/glyph_atlas.h"
#include "ui/render/text_layout.h"

#include <engine/resources/asset_id.h>
#include <engine/resources/font.h>
#include <engine/ui/document.h>
#include <engine/ui/text_line.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace engine::ui::math {
class MathFont;
}

namespace engine::ui {

// The UI renderer's fonts and glyph atlas, on the CPU: one instance shared by every window, main thread only, no GPU
// handle inside (a device keeps its own textures and catches up with `atlas()`'s versions).
//
// A font registers by AssetId. A font that is not registered resolves to the builtin UI font (`builtin::font_ui`).
// `builtin::font_math` is not a text face: add_font hands it to
// `math::MathFont` (formulas are drawn as outlines through fill_path), and `math_font()` returns it so the painter
// can answer IUiPainter::math_font from here.
class FontLibrary {
public:
    explicit FontLibrary(int atlas_page_size = GlyphAtlas::kDefaultPageSize);
    ~FontLibrary();

    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    // False when the bytes are empty or not a font; true when `id` is already registered.
    [[nodiscard]] bool add_font(AssetId id, const Font& font);
    [[nodiscard]] bool has_font(AssetId id) const noexcept;
    // The face for `id`, else the builtin UI font's, else nullptr.
    [[nodiscard]] FontFace* face(AssetId id) noexcept;
    [[nodiscard]] const math::MathFont* math_font() const noexcept { return math_font_.get(); }
    [[nodiscard]] GlyphAtlas& atlas() noexcept { return atlas_; }
    [[nodiscard]] const GlyphAtlas& atlas() const noexcept { return atlas_; }

    // The painter's text queries (text_layout.h), by AssetId. Without any face they return the no-face answers: half
    // an em per byte wide, `size` high, and IUiPainter's default wrap and metrics.
    [[nodiscard]] TextFontMetrics font_metrics(AssetId font, float size);
    [[nodiscard]] glm::vec2 measure_text(std::string_view text, AssetId font, float size);
    [[nodiscard]] TextBlock break_lines(std::string_view text, AssetId font, float size, float max_width);
    // layout_glyphs for `font`; appends nothing without a face.
    float layout_text(std::string_view text, AssetId font, float size, glm::vec2 position, UiAlign horizontal,
            UiAlign vertical, float raster_scale, std::vector<GlyphQuad>& out);

private:
    struct Entry {
        AssetId id;
        std::uint32_t atlas_id = 0;
        FontFace face;
    };

    [[nodiscard]] Entry* find(AssetId id) noexcept;
    // `find`, falling back to the builtin UI font.
    [[nodiscard]] Entry* resolve(AssetId id) noexcept;

    // A handful of fonts: a linear scan beats hashing the id on every measure.
    std::vector<std::unique_ptr<Entry>> faces_;
    std::unique_ptr<math::MathFont> math_font_;
    GlyphAtlas atlas_;
};

}
