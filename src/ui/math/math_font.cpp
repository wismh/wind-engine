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

#include "ui/math/math_font.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

namespace engine::ui::math {
namespace {

// Big-endian reads with a sticky failure flag: one `ok` check after a whole table parse instead of a
// branch per field, and no read ever leaves `data`.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

    [[nodiscard]] bool ok() const noexcept {
        return ok_;
    }

    void fail() noexcept {
        ok_ = false;
    }

    std::uint16_t u16(std::size_t offset) {
        if (offset + 2 > data_.size()) {
            ok_ = false;
            return 0;
        }
        return static_cast<std::uint16_t>((data_[offset] << 8) | data_[offset + 1]);
    }

    std::int16_t i16(std::size_t offset) {
        return static_cast<std::int16_t>(u16(offset));
    }

    // MathValueRecord = int16 value + Offset16 deviceTable (device tables are ignored).
    float math_value(std::size_t offset) {
        return static_cast<float>(i16(offset));
    }

private:
    std::span<const std::uint8_t> data_;
    bool ok_ = true;
};

constexpr std::size_t kMathValueRecordSize = 4;

// The 51 MathValueRecords of `MathConstants`, in spec order, after the four leading 16-bit fields.
constexpr std::array<float MathConstants::*, 51> kValueRecordFields{
        &MathConstants::math_leading,
        &MathConstants::axis_height,
        &MathConstants::accent_base_height,
        &MathConstants::flattened_accent_base_height,
        &MathConstants::subscript_shift_down,
        &MathConstants::subscript_top_max,
        &MathConstants::subscript_baseline_drop_min,
        &MathConstants::superscript_shift_up,
        &MathConstants::superscript_shift_up_cramped,
        &MathConstants::superscript_bottom_min,
        &MathConstants::superscript_baseline_drop_max,
        &MathConstants::sub_superscript_gap_min,
        &MathConstants::superscript_bottom_max_with_subscript,
        &MathConstants::space_after_script,
        &MathConstants::upper_limit_gap_min,
        &MathConstants::upper_limit_baseline_rise_min,
        &MathConstants::lower_limit_gap_min,
        &MathConstants::lower_limit_baseline_drop_min,
        &MathConstants::stack_top_shift_up,
        &MathConstants::stack_top_display_style_shift_up,
        &MathConstants::stack_bottom_shift_down,
        &MathConstants::stack_bottom_display_style_shift_down,
        &MathConstants::stack_gap_min,
        &MathConstants::stack_display_style_gap_min,
        &MathConstants::stretch_stack_top_shift_up,
        &MathConstants::stretch_stack_bottom_shift_down,
        &MathConstants::stretch_stack_gap_above_min,
        &MathConstants::stretch_stack_gap_below_min,
        &MathConstants::fraction_numerator_shift_up,
        &MathConstants::fraction_numerator_display_style_shift_up,
        &MathConstants::fraction_denominator_shift_down,
        &MathConstants::fraction_denominator_display_style_shift_down,
        &MathConstants::fraction_numerator_gap_min,
        &MathConstants::fraction_num_display_style_gap_min,
        &MathConstants::fraction_rule_thickness,
        &MathConstants::fraction_denominator_gap_min,
        &MathConstants::fraction_denom_display_style_gap_min,
        &MathConstants::skewed_fraction_horizontal_gap,
        &MathConstants::skewed_fraction_vertical_gap,
        &MathConstants::overbar_vertical_gap,
        &MathConstants::overbar_rule_thickness,
        &MathConstants::overbar_extra_ascender,
        &MathConstants::underbar_vertical_gap,
        &MathConstants::underbar_rule_thickness,
        &MathConstants::underbar_extra_descender,
        &MathConstants::radical_vertical_gap,
        &MathConstants::radical_display_style_vertical_gap,
        &MathConstants::radical_rule_thickness,
        &MathConstants::radical_extra_ascender,
        &MathConstants::radical_kern_before_degree,
        &MathConstants::radical_kern_after_degree,
};

MathConstants parse_constants(Reader& r, std::size_t base) {
    MathConstants c;
    c.script_percent_scale_down = r.i16(base);
    c.script_script_percent_scale_down = r.i16(base + 2);
    c.delimited_sub_formula_min_height = static_cast<float>(r.u16(base + 4));
    c.display_operator_min_height = static_cast<float>(r.u16(base + 6));
    std::size_t at = base + 8;
    for (float MathConstants::* field : kValueRecordFields) {
        c.*field = r.math_value(at);
        at += kMathValueRecordSize;
    }
    c.radical_degree_bottom_raise_percent = r.i16(at);
    return c;
}

// Coverage table -> glyph ids ordered by coverage index (so `result[i]` pairs with the i-th record of
// the table the coverage belongs to).
std::vector<GlyphId> parse_coverage(Reader& r, std::size_t base) {
    std::vector<GlyphId> glyphs;
    const std::uint16_t format = r.u16(base);
    if (format == 1) {
        const std::uint16_t count = r.u16(base + 2);
        glyphs.reserve(count);
        for (std::uint16_t i = 0; i < count && r.ok(); ++i) {
            glyphs.push_back(r.u16(base + 4 + std::size_t{i} * 2));
        }
    } else if (format == 2) {
        const std::uint16_t ranges = r.u16(base + 2);
        for (std::uint16_t i = 0; i < ranges && r.ok(); ++i) {
            const std::size_t at = base + 4 + std::size_t{i} * 6;
            const std::uint16_t first = r.u16(at);
            const std::uint16_t last = r.u16(at + 2);
            const std::uint16_t start_index = r.u16(at + 4);
            if (last < first) {
                continue;
            }
            const std::size_t end_index = std::size_t{start_index} + (last - first) + 1;
            if (glyphs.size() < end_index) {
                glyphs.resize(end_index, 0);
            }
            for (std::uint32_t g = first; g <= last; ++g) {
                glyphs[start_index + (g - first)] = static_cast<GlyphId>(g);
            }
        }
    } else {
        r.fail();
    }
    return glyphs;
}

// MathItalicsCorrectionInfo and MathTopAccentAttachment share one layout: Offset16 coverage (relative
// to this table), uint16 count, MathValueRecord[count].
std::unordered_map<GlyphId, float> parse_glyph_values(Reader& r, std::size_t base) {
    std::unordered_map<GlyphId, float> values;
    const std::uint16_t coverage_offset = r.u16(base);
    const std::uint16_t count = r.u16(base + 2);
    if (!r.ok()) {
        return values;
    }
    const std::vector<GlyphId> glyphs = parse_coverage(r, base + coverage_offset);
    for (std::size_t i = 0; i < glyphs.size() && i < count && r.ok(); ++i) {
        values[glyphs[i]] = r.math_value(base + 4 + i * kMathValueRecordSize);
    }
    return values;
}

GlyphConstruction parse_construction(Reader& r, std::size_t base) {
    GlyphConstruction construction;
    const std::uint16_t assembly_offset = r.u16(base);
    const std::uint16_t variant_count = r.u16(base + 2);
    construction.variants.reserve(variant_count);
    for (std::uint16_t i = 0; i < variant_count && r.ok(); ++i) {
        const std::size_t at = base + 4 + std::size_t{i} * 4;
        construction.variants.push_back({r.u16(at), static_cast<float>(r.u16(at + 2))});
    }
    if (assembly_offset != 0) {
        const std::size_t assembly_base = base + assembly_offset;
        GlyphAssembly assembly;
        assembly.italics_correction = r.math_value(assembly_base);
        const std::uint16_t part_count = r.u16(assembly_base + 4);
        assembly.parts.reserve(part_count);
        for (std::uint16_t i = 0; i < part_count && r.ok(); ++i) {
            const std::size_t at = assembly_base + 6 + std::size_t{i} * 10;
            AssemblyPart part;
            part.glyph = r.u16(at);
            part.start_connector_length = static_cast<float>(r.u16(at + 2));
            part.end_connector_length = static_cast<float>(r.u16(at + 4));
            part.full_advance = static_cast<float>(r.u16(at + 6));
            part.extender = (r.u16(at + 8) & 1u) != 0;
            assembly.parts.push_back(part);
        }
        construction.assembly = std::move(assembly);
    }
    return construction;
}

using ConstructionMap = std::unordered_map<GlyphId, GlyphConstruction>;

void parse_constructions(Reader& r, std::size_t variants_base, std::size_t coverage_offset_at,
        std::size_t count_at, std::size_t offsets_at, ConstructionMap& out) {
    const std::uint16_t coverage_offset = r.u16(coverage_offset_at);
    const std::uint16_t count = r.u16(count_at);
    if (!r.ok() || count == 0 || coverage_offset == 0) {
        return;
    }
    const std::vector<GlyphId> glyphs = parse_coverage(r, variants_base + coverage_offset);
    for (std::size_t i = 0; i < glyphs.size() && i < count && r.ok(); ++i) {
        const std::uint16_t construction_offset = r.u16(offsets_at + i * 2);
        out[glyphs[i]] = parse_construction(r, variants_base + construction_offset);
    }
}

}

struct MathFont::Impl {
    std::vector<std::uint8_t> data;
    stbtt_fontinfo info{};
    float units_per_em = 1000.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    MathConstants constants;
    float min_connector_overlap = 0.0f;
    std::unordered_map<GlyphId, float> italics_correction;
    std::unordered_map<GlyphId, float> top_accent_attachment;
    ConstructionMap vertical;
    ConstructionMap horizontal;
    mutable std::unordered_map<GlyphId, std::vector<OutlineCommand>> outline_cache;
};

MathFont::MathFont(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MathFont::MathFont(MathFont&&) noexcept = default;
MathFont& MathFont::operator=(MathFont&&) noexcept = default;
MathFont::~MathFont() = default;

std::expected<MathFont, MathFontError> MathFont::load(std::span<const std::uint8_t> bytes) {
    auto impl = std::make_unique<Impl>();
    impl->data.assign(bytes.begin(), bytes.end());
    const unsigned char* data = impl->data.data();
    if (impl->data.empty()) {
        return std::unexpected(MathFontError::InvalidFont);
    }
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || stbtt_InitFont(&impl->info, data, offset) == 0) {
        return std::unexpected(MathFontError::InvalidFont);
    }

    const stbtt_uint32 math_table = stbtt__find_table(const_cast<stbtt_uint8*>(data), impl->info.fontstart, "MATH");
    if (math_table == 0) {
        return std::unexpected(MathFontError::MissingMathTable);
    }

    // head.unitsPerEm via stb: the scale for a pixel height equals pixels / (ascent - descent), so
    // recover units from the em-scale helper instead of re-reading `head` by hand.
    const float em_scale = stbtt_ScaleForMappingEmToPixels(&impl->info, 1.0f);
    impl->units_per_em = em_scale > 0.0f ? 1.0f / em_scale : 1000.0f;
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&impl->info, &ascent, &descent, &line_gap);
    impl->ascent = static_cast<float>(ascent);
    impl->descent = static_cast<float>(descent);

    Reader r(std::span<const std::uint8_t>(impl->data.data(), impl->data.size()));
    const std::size_t base = math_table;
    const std::uint16_t constants_offset = r.u16(base + 4);
    const std::uint16_t glyph_info_offset = r.u16(base + 6);
    const std::uint16_t variants_offset = r.u16(base + 8);
    if (!r.ok() || constants_offset == 0) {
        return std::unexpected(MathFontError::MalformedMathTable);
    }

    impl->constants = parse_constants(r, base + constants_offset);

    if (glyph_info_offset != 0) {
        const std::size_t info_base = base + glyph_info_offset;
        const std::uint16_t italics_offset = r.u16(info_base);
        const std::uint16_t accent_offset = r.u16(info_base + 2);
        if (italics_offset != 0) {
            impl->italics_correction = parse_glyph_values(r, info_base + italics_offset);
        }
        if (accent_offset != 0) {
            impl->top_accent_attachment = parse_glyph_values(r, info_base + accent_offset);
        }
    }

    if (variants_offset != 0) {
        const std::size_t variants_base = base + variants_offset;
        impl->min_connector_overlap = static_cast<float>(r.u16(variants_base));
        const std::uint16_t vertical_count = r.u16(variants_base + 6);
        parse_constructions(r, variants_base, variants_base + 2, variants_base + 6, variants_base + 10,
                impl->vertical);
        parse_constructions(r, variants_base, variants_base + 4, variants_base + 8,
                variants_base + 10 + std::size_t{vertical_count} * 2, impl->horizontal);
    }

    if (!r.ok()) {
        return std::unexpected(MathFontError::MalformedMathTable);
    }
    return MathFont(std::move(impl));
}

float MathFont::units_per_em() const noexcept {
    return impl_->units_per_em;
}

float MathFont::ascent() const noexcept {
    return impl_->ascent;
}

float MathFont::descent() const noexcept {
    return impl_->descent;
}

GlyphId MathFont::glyph_index(char32_t codepoint) const {
    return static_cast<GlyphId>(stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(codepoint)));
}

GlyphMetrics MathFont::metrics(GlyphId glyph) const {
    GlyphMetrics m;
    int advance = 0;
    int left_bearing = 0;
    stbtt_GetGlyphHMetrics(&impl_->info, glyph, &advance, &left_bearing);
    m.advance = static_cast<float>(advance);
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (stbtt_GetGlyphBox(&impl_->info, glyph, &x0, &y0, &x1, &y1) != 0) {
        m.x_min = static_cast<float>(x0);
        m.y_min = static_cast<float>(y0);
        m.x_max = static_cast<float>(x1);
        m.y_max = static_cast<float>(y1);
    }
    return m;
}

std::vector<OutlineCommand> MathFont::outline(GlyphId glyph) const {
    stbtt_vertex* vertices = nullptr;
    const int count = stbtt_GetGlyphShape(&impl_->info, glyph, &vertices);
    std::vector<OutlineCommand> commands;
    commands.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) {
        const stbtt_vertex& v = vertices[i];
        OutlineCommand command;
        command.p = {static_cast<float>(v.x), static_cast<float>(v.y)};
        command.c1 = {static_cast<float>(v.cx), static_cast<float>(v.cy)};
        command.c2 = {static_cast<float>(v.cx1), static_cast<float>(v.cy1)};
        switch (v.type) {
            case STBTT_vmove:
                command.kind = OutlineCommand::Kind::Move;
                break;
            case STBTT_vline:
                command.kind = OutlineCommand::Kind::Line;
                break;
            case STBTT_vcurve:
                command.kind = OutlineCommand::Kind::Quad;
                break;
            case STBTT_vcubic:
                command.kind = OutlineCommand::Kind::Cubic;
                break;
            default:
                continue;
        }
        commands.push_back(command);
    }
    stbtt_FreeShape(&impl_->info, vertices);
    return commands;
}

const std::vector<OutlineCommand>& MathFont::outline_cached(GlyphId glyph) const {
    const auto it = impl_->outline_cache.find(glyph);
    if (it != impl_->outline_cache.end()) {
        return it->second;
    }
    return impl_->outline_cache.emplace(glyph, outline(glyph)).first->second;
}

const MathConstants& MathFont::constants() const noexcept {
    return impl_->constants;
}

float MathFont::min_connector_overlap() const noexcept {
    return impl_->min_connector_overlap;
}

std::optional<float> MathFont::italics_correction(GlyphId glyph) const {
    const auto it = impl_->italics_correction.find(glyph);
    return it == impl_->italics_correction.end() ? std::nullopt : std::optional<float>(it->second);
}

std::optional<float> MathFont::top_accent_attachment(GlyphId glyph) const {
    const auto it = impl_->top_accent_attachment.find(glyph);
    return it == impl_->top_accent_attachment.end() ? std::nullopt : std::optional<float>(it->second);
}

const GlyphConstruction* MathFont::vertical_construction(GlyphId glyph) const {
    const auto it = impl_->vertical.find(glyph);
    return it == impl_->vertical.end() ? nullptr : &it->second;
}

const GlyphConstruction* MathFont::horizontal_construction(GlyphId glyph) const {
    const auto it = impl_->horizontal.find(glyph);
    return it == impl_->horizontal.end() ? nullptr : &it->second;
}

}
