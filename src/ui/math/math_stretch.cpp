#include "ui/math/math_stretch.h"

#include <algorithm>
#include <limits>

namespace engine::ui::math {
namespace {

constexpr int kMaxExtenderRepeats = 64;

StretchedGlyph single_glyph(const MathFont& font, GlyphId glyph) {
    const GlyphMetrics m = font.metrics(glyph);
    StretchedGlyph result;
    result.pieces.push_back({glyph, 0.0f});
    result.width = m.advance;
    result.bottom = m.y_min;
    result.top = m.y_max;
    return result;
}

StretchedGlyph assemble(const MathFont& font, const GlyphAssembly& assembly, float min_size) {
    const std::vector<AssemblyPart>& parts = assembly.parts;
    const float min_overlap = font.min_connector_overlap();

    const auto sequence = [&](int repeats) {
        std::vector<const AssemblyPart*> seq;
        for (const AssemblyPart& part : parts) {
            const int copies = part.extender ? repeats : 1;
            for (int i = 0; i < copies; ++i) {
                seq.push_back(&part);
            }
        }
        return seq;
    };
    const auto total_advance = [](const std::vector<const AssemblyPart*>& seq) {
        float sum = 0.0f;
        for (const AssemblyPart* part : seq) {
            sum += part->full_advance;
        }
        return sum;
    };

    // Fewest extender repeats (at least one) whose stack, at the tightest allowed overlap, reaches the target.
    std::vector<const AssemblyPart*> seq = sequence(1);
    const bool has_extender =
            std::any_of(parts.begin(), parts.end(), [](const AssemblyPart& p) { return p.extender; });
    for (int repeats = 1; has_extender && repeats < kMaxExtenderRepeats; ++repeats) {
        seq = sequence(repeats);
        const float tightest = total_advance(seq) - static_cast<float>(seq.size() - 1) * min_overlap;
        if (tightest >= min_size) {
            break;
        }
    }

    // One overlap for every joint: wide enough to land on the target, never past what the narrowest
    // joint's connectors allow, never below the font's minimum.
    float overlap = 0.0f;
    if (seq.size() > 1) {
        float cap = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i + 1 < seq.size(); ++i) {
            cap = std::min({cap, seq[i]->end_connector_length, seq[i + 1]->start_connector_length});
        }
        const float wanted = (total_advance(seq) - min_size) / static_cast<float>(seq.size() - 1);
        overlap = std::clamp(wanted, min_overlap, std::max(cap, min_overlap));
    }

    StretchedGlyph result;
    result.assembled = true;
    result.bottom = 0.0f;
    result.top = 0.0f;
    bool first = true;
    float cursor = 0.0f;
    for (const AssemblyPart* part : seq) {
        const GlyphMetrics m = font.metrics(part->glyph);
        // A part's bottom edge (its ink bottom) sits at the cursor; `full_advance` is how far it spans.
        const float origin = cursor - m.y_min;
        result.pieces.push_back({part->glyph, origin});
        result.width = std::max(result.width, m.advance);
        const float low = origin + m.y_min;
        const float high = origin + m.y_max;
        result.bottom = first ? low : std::min(result.bottom, low);
        result.top = first ? high : std::max(result.top, high);
        first = false;
        cursor += part->full_advance - overlap;
    }
    return result;
}

}

StretchedGlyph stretch_vertical(const MathFont& font, char32_t codepoint, float min_size) {
    const GlyphId base = font.glyph_index(codepoint);
    const GlyphConstruction* construction = font.vertical_construction(base);
    if (construction == nullptr) {
        return single_glyph(font, base);
    }
    for (const GlyphVariant& variant : construction->variants) {
        if (variant.advance >= min_size) {
            return single_glyph(font, variant.glyph);
        }
    }
    if (construction->assembly && !construction->assembly->parts.empty()) {
        return assemble(font, *construction->assembly, min_size);
    }
    if (!construction->variants.empty()) {
        return single_glyph(font, construction->variants.back().glyph);
    }
    return single_glyph(font, base);
}

}
