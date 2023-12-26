#pragma once

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace engine::ui::math {

enum class MathFontError {
    InvalidFont,
    MissingMathTable,
    MalformedMathTable,
};

using GlyphId = std::uint16_t;

// Every `float` below is in font design units (`MathFont::units_per_em()` per em, y up), exactly as
// stored in the font. The layout stage scales by size / units_per_em; nothing here knows a size.

// OpenType MATH `MathConstants`. Field order follows the spec; `*_percent_*` fields are percentages,
// `delimited_sub_formula_min_height` / `display_operator_min_height` are heights, all others are
// MathValueRecord values (device tables ignored).
struct MathConstants {
    int script_percent_scale_down = 0;
    int script_script_percent_scale_down = 0;
    int radical_degree_bottom_raise_percent = 0;
    float delimited_sub_formula_min_height = 0.0f;
    float display_operator_min_height = 0.0f;
    float math_leading = 0.0f;
    float axis_height = 0.0f;
    float accent_base_height = 0.0f;
    float flattened_accent_base_height = 0.0f;
    float subscript_shift_down = 0.0f;
    float subscript_top_max = 0.0f;
    float subscript_baseline_drop_min = 0.0f;
    float superscript_shift_up = 0.0f;
    float superscript_shift_up_cramped = 0.0f;
    float superscript_bottom_min = 0.0f;
    float superscript_baseline_drop_max = 0.0f;
    float sub_superscript_gap_min = 0.0f;
    float superscript_bottom_max_with_subscript = 0.0f;
    float space_after_script = 0.0f;
    float upper_limit_gap_min = 0.0f;
    float upper_limit_baseline_rise_min = 0.0f;
    float lower_limit_gap_min = 0.0f;
    float lower_limit_baseline_drop_min = 0.0f;
    float stack_top_shift_up = 0.0f;
    float stack_top_display_style_shift_up = 0.0f;
    float stack_bottom_shift_down = 0.0f;
    float stack_bottom_display_style_shift_down = 0.0f;
    float stack_gap_min = 0.0f;
    float stack_display_style_gap_min = 0.0f;
    float stretch_stack_top_shift_up = 0.0f;
    float stretch_stack_bottom_shift_down = 0.0f;
    float stretch_stack_gap_above_min = 0.0f;
    float stretch_stack_gap_below_min = 0.0f;
    float fraction_numerator_shift_up = 0.0f;
    float fraction_numerator_display_style_shift_up = 0.0f;
    float fraction_denominator_shift_down = 0.0f;
    float fraction_denominator_display_style_shift_down = 0.0f;
    float fraction_numerator_gap_min = 0.0f;
    float fraction_num_display_style_gap_min = 0.0f;
    float fraction_rule_thickness = 0.0f;
    float fraction_denominator_gap_min = 0.0f;
    float fraction_denom_display_style_gap_min = 0.0f;
    float skewed_fraction_horizontal_gap = 0.0f;
    float skewed_fraction_vertical_gap = 0.0f;
    float overbar_vertical_gap = 0.0f;
    float overbar_rule_thickness = 0.0f;
    float overbar_extra_ascender = 0.0f;
    float underbar_vertical_gap = 0.0f;
    float underbar_rule_thickness = 0.0f;
    float underbar_extra_descender = 0.0f;
    float radical_vertical_gap = 0.0f;
    float radical_display_style_vertical_gap = 0.0f;
    float radical_rule_thickness = 0.0f;
    float radical_extra_ascender = 0.0f;
    float radical_kern_before_degree = 0.0f;
    float radical_kern_after_degree = 0.0f;
};

struct GlyphMetrics {
    float advance = 0.0f;
    // Ink bounding box (y up). All zero for an empty glyph such as a space.
    float x_min = 0.0f;
    float y_min = 0.0f;
    float x_max = 0.0f;
    float y_max = 0.0f;
};

// One segment of a glyph outline. `Move` starts a new contour (the previous one closes implicitly);
// `Quad` uses `c1` as its control point, `Cubic` uses `c1` and `c2` (CFF outlines are cubic).
struct OutlineCommand {
    enum class Kind : std::uint8_t {
        Move,
        Line,
        Quad,
        Cubic,
    };

    Kind kind = Kind::Move;
    glm::vec2 p{};
    glm::vec2 c1{};
    glm::vec2 c2{};
};

// MATH `MathGlyphConstruction` variants, smallest first by `advance` along the stretch axis.
struct GlyphVariant {
    GlyphId glyph = 0;
    float advance = 0.0f;
};

struct AssemblyPart {
    GlyphId glyph = 0;
    float start_connector_length = 0.0f;
    float end_connector_length = 0.0f;
    float full_advance = 0.0f;
    bool extender = false;
};

// Parts in bottom-to-top (vertical) / left-to-right (horizontal) order, per the MATH spec.
struct GlyphAssembly {
    float italics_correction = 0.0f;
    std::vector<AssemblyPart> parts;
};

struct GlyphConstruction {
    std::vector<GlyphVariant> variants;
    std::optional<GlyphAssembly> assembly;
};

// A parsed OpenType MATH font: cmap, glyph metrics/outlines (through the vendored stb_truetype, TrueType
// and CFF alike) plus the MATH table (constants, italics correction, top-accent attachment, size
// variants and glyph assemblies). Pure CPU: no GPU, no window, no NanoVG, so formula layout built on it is
// testable headless. stb_truetype itself is not hardened against hostile font bytes — feed it fonts from
// the asset catalog only. The MATH table parser, in contrast, bounds-checks every read.
class MathFont {
public:
    // Copies `bytes`. InvalidFont: not a font stb_truetype accepts. MissingMathTable: a valid font with
    // no `MATH` table (an ordinary text font). MalformedMathTable: `MATH` offsets point outside the data.
    [[nodiscard]] static std::expected<MathFont, MathFontError> load(std::span<const std::uint8_t> bytes);

    MathFont(MathFont&&) noexcept;
    MathFont& operator=(MathFont&&) noexcept;
    ~MathFont();
    MathFont(const MathFont&) = delete;
    MathFont& operator=(const MathFont&) = delete;

    [[nodiscard]] float units_per_em() const noexcept;
    [[nodiscard]] float ascent() const noexcept;
    [[nodiscard]] float descent() const noexcept;  // negative, below the baseline

    // 0 means the font has no glyph for `codepoint` (`.notdef`).
    [[nodiscard]] GlyphId glyph_index(char32_t codepoint) const;
    [[nodiscard]] GlyphMetrics metrics(GlyphId glyph) const;
    [[nodiscard]] std::vector<OutlineCommand> outline(GlyphId glyph) const;
    // The same outline, read from the font once and kept: what a painter that redraws every frame wants. The
    // reference stays valid for the lifetime of the font. Not thread-safe (engine UI is main-thread only).
    [[nodiscard]] const std::vector<OutlineCommand>& outline_cached(GlyphId glyph) const;

    [[nodiscard]] const MathConstants& constants() const noexcept;
    [[nodiscard]] float min_connector_overlap() const noexcept;
    [[nodiscard]] std::optional<float> italics_correction(GlyphId glyph) const;
    [[nodiscard]] std::optional<float> top_accent_attachment(GlyphId glyph) const;
    // nullptr when the glyph has no stretchy construction on that axis.
    [[nodiscard]] const GlyphConstruction* vertical_construction(GlyphId glyph) const;
    [[nodiscard]] const GlyphConstruction* horizontal_construction(GlyphId glyph) const;

private:
    struct Impl;
    explicit MathFont(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
