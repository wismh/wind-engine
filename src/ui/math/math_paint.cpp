#include "ui/math/math_paint.h"

#include <vector>

namespace engine::ui::math {
namespace {

PathSegment::Kind to_path_kind(OutlineCommand::Kind kind) {
    switch (kind) {
        case OutlineCommand::Kind::Move:
            return PathSegment::Kind::Move;
        case OutlineCommand::Kind::Line:
            return PathSegment::Kind::Line;
        case OutlineCommand::Kind::Quad:
            return PathSegment::Kind::Quad;
        case OutlineCommand::Kind::Cubic:
            return PathSegment::Kind::Cubic;
    }
    return PathSegment::Kind::Line;
}

}

void paint_layout(IUiPainter& painter, const MathFont& font, const MathLayout& layout, glm::vec2 origin, float scale,
        glm::vec4 color) {
    std::vector<PathSegment> path;
    for (const PlacedGlyph& glyph : layout.glyphs) {
        // Font points are y-up in design units; the layout is y-down in layout units. Both steps, plus the
        // canvas scale, are one affine map per point.
        const glm::vec2 glyph_origin = origin + glyph.origin * scale;
        const float unit = glyph.scale * scale;
        const auto map = [&](glm::vec2 point) {
            return glm::vec2{glyph_origin.x + point.x * unit, glyph_origin.y - point.y * unit};
        };
        const std::vector<OutlineCommand>& outline = font.outline_cached(glyph.glyph);
        path.reserve(path.size() + outline.size());
        for (const OutlineCommand& command : outline) {
            path.push_back({to_path_kind(command.kind), map(command.p), map(command.c1), map(command.c2)});
        }
    }
    if (!path.empty()) {
        painter.fill_path(path, color);
    }
    for (const PlacedRule& rule : layout.rules) {
        const glm::vec2 top_left = origin + rule.position * scale;
        const render::Rect rect{top_left.x, top_left.y, rule.size.x * scale, rule.size.y * scale};
        painter.fill_rounded_rect(rect, 0.0f, color);
    }
}

}
