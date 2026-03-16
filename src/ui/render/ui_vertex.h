#pragma once

// The one vertex every UI draw is made of. docs/tech/architecture/UI Performance Plan.md, step 5 (Design, wind-234).
// The fragment math that reads it is fragment.h; the pipeline that carries it (attribute formats, blend, samplers) is
// ui_pipeline.h. A backend implements those two and nothing else.

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>

namespace engine::ui {

// What the fragment stage does with a vertex (flat across a quad). Units called "px" below are device pixels: logical
// units times the frame's pixel ratio times the transform's scale, so a coverage ramp one px wide is NanoVG's fringe.
enum class UiShapeKind : std::uint32_t {
    // Rounded (or sharp) rect fill. local: px from the rect center, in the rect's own (unrotated) frame.
    // shape: half size xy, corner radii zw (x and y radius, NanoVG clamps them separately; 0 = sharp corners).
    Fill = 0,
    // The same rect stroked, centered on its edge. aux.x: half the stroke width.
    Stroke = 1,
    // Butt-capped segment. local: px along (x) and across (y) the segment from its midpoint. shape: half length, half
    // width.
    Line = 2,
    // Butt-capped arc of a ring. local: px from the center, the sweep's middle direction on +y. shape: radius, half
    // width, half sweep (radians; pi or more is the whole ring).
    Arc = 3,
    // Coverage from the glyph page (Glyph slot) at uv.
    Glyph = 4,
    // Coverage from a path mask page (Mask slot) at uv.
    Mask = 5,
    // Straight-alpha texel from an image page (Image slot), premultiplied after the fetch, under the rect coverage of
    // Fill with sharp corners. uv: image coordinates, 0..1 across the whole image, clamped. shape.zw: the image's
    // texel rect origin on the page (normalized), aux: its size (normalized).
    Image = 6,
    // Image, but uv wraps (fract) instead of clamping: background-repeat.
    ImageRepeat = 7,
    // Gradient fills: Fill's coverage (shape as Fill) times a texel of a ramp row (Ramp slot). aux.x: the row's v.
    // Linear: t = uv.x. Radial: t = length(uv) + aux.y. Conic: t = atan2(uv.x, -uv.y) / 2pi, wrapped into [0, 1).
    LinearGradient = 8,
    RadialGradient = 9,
    ConicGradient = 10,
};

// 72 bytes. Every field is per-vertex because there are no per-draw uniforms: a batch spans any number of shapes with
// different clips, transforms, colors, and kinds, and only a texture change ends it. Smaller layouts were weighed:
// the clip needs its half extent next to the position (4 floats) for the antialiased discard, the widest kinds
// (stroke, image repeat, gradients) need 4 shape floats plus 2 more, and the color packs into 4 bytes. A quad is
// 4 vertices and 6 indices (312 bytes); a HUD of 3,000 quads uploads under 1 MB a frame.
struct UiVertex {
    // Logical units of the frame (begin_frame's width x height, origin top left, y down). The device maps them to
    // clip space like NanoVG's viewSize did.
    glm::vec2 position{};
    // Kind-specific: texture coordinates on the bound page (Glyph, Mask), image coordinates (Image, ImageRepeat),
    // gradient coordinates (gradients).
    glm::vec2 uv{};
    // Position in the shape's frame, px.
    glm::vec2 local{};
    // Shape parameters, px (see UiShapeKind).
    glm::vec4 shape{};
    // xy: position in the clip rect's frame, px from its center; zw: the clip's half extent, px. A vertex outside
    // every clip has a huge extent.
    glm::vec4 clip{};
    // Kind-specific extras (see UiShapeKind).
    glm::vec2 aux{};
    // Premultiplied RGBA8, red in the low byte, the painter's opacity folded in.
    std::uint32_t color = 0;
    UiShapeKind kind = UiShapeKind::Fill;
};

static_assert(sizeof(UiVertex) == 72, "UiVertex layout is part of the device contract (ui_pipeline.h)");
static_assert(offsetof(UiVertex, color) == 64);
static_assert(offsetof(UiVertex, kind) == 68);

// Packs a straight-alpha color times `opacity` into UiVertex::color: premultiplied, each channel clamped to 0..1 and
// rounded to the nearest of 255 steps.
[[nodiscard]] std::uint32_t pack_premultiplied(glm::vec4 color, float opacity) noexcept;
// UiVertex::color back to 0..1 floats (still premultiplied).
[[nodiscard]] glm::vec4 unpack_color(std::uint32_t color) noexcept;

}
