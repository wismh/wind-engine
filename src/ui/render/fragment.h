#pragma once

// The UI fragment stage, written once on the CPU. Every device's shader implements exactly this math (the OpenGL one
// line for line); engine_tests run it to check what a vertex means without a GPU. Antialiasing is a one px linear ramp
// centered on every edge (the fill fringe and the stroke mask) and the same ramp on a scissor.
//
// Per fragment, with `v` the interpolated vertex (UiVertex, color unpacked to premultiplied floats):
//
//   clip_cov = clamp(0.5 - (|v.clip.x| - v.clip.z), 0, 1) * clamp(0.5 - (|v.clip.y| - v.clip.w), 0, 1)
//
//   box(p, h, r)        (signed distance to a rect of half size h centered at 0, px)
//       r.x <= 0:              max(|p.x| - h.x, |p.y| - h.y)                     sharp: miter corners
//       q = |p| - (h - r); q.x > 0 && q.y > 0:
//                              k0 = length(q / r); k1 = length(q / (r * r)); k0 * (k0 - 1) / k1
//                                                                               (elliptic corner; |q| - r when round)
//       otherwise:             max(|p.x| - h.x, |p.y| - h.y)
//
//   Fill            cov = clamp(0.5 - box(local, shape.xy, shape.zw), 0, 1);            out = color * cov
//   Stroke          cov = clamp(aux.x + 0.5 - |box(local, shape.xy, shape.zw)|, 0, 1);  out = color * cov
//   Line            cov = clamp(shape.y + 0.5 - |local.y|, 0, 1) * clamp(shape.x + 0.5 - |local.x|, 0, 1)
//   Arc             r = length(local); cov = clamp(shape.y + 0.5 - |r - shape.x|, 0, 1)
//                   if shape.z < pi: gap = shape.z - atan2(|local.x|, local.y)
//                                    if gap < pi/2: cov *= clamp(0.5 + r * sin(gap), 0, 1)
//   Glyph, Mask     cov = texture(slot, uv).r;                                          out = color * cov
//   Image           t = clamp(uv, 0, 1)            (ImageRepeat: t = fract(uv))
//                   texel = texture(u_images, shape.zw + t * aux); texel.rgb *= texel.a
//                   out = texel * color * clamp(0.5 - box(local, shape.xy, 0), 0, 1)     (half size: Fill's)
//   Gradients       t: Linear uv.x; Radial length(uv) + aux.y; Conic fract(atan2(uv.x, -uv.y) / 2pi)
//                   texel = texture(u_ramps, ((clamp(t, 0, 1) * (kRampWidth - 1) + 0.5) / kRampWidth, aux.x))
//                   out = texel * color * clamp(0.5 - box(local, shape.xy, shape.zw), 0, 1)
//
//   result = out * clip_cov        (premultiplied; blended One, OneMinusSrcAlpha)
//
// The only constant is kRampWidth. Everything else, clip included, comes from the vertex, so nothing but a texture
// change can end a batch.

#include "ui/render/ui_pipeline.h"
#include "ui/render/ui_vertex.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <optional>

namespace engine::ui {

// Width of a gradient ramp row, texels (RampAtlas). A shader constant, not per vertex.
inline constexpr int kRampWidth = 256;

// The interpolated inputs of one fragment: UiVertex with the color unpacked.
struct UiFragment {
    glm::vec2 uv{};
    glm::vec2 local{};
    glm::vec4 shape{};
    glm::vec4 clip{};
    glm::vec2 aux{};
    glm::vec4 color{};
    UiShapeKind kind = UiShapeKind::Fill;
};

// Where a fragment samples: a slot and normalized coordinates on that slot's bound page.
struct UiTexelFetch {
    UiTextureSlot slot = UiTextureSlot::Glyph;
    glm::vec2 uv{};
};

[[nodiscard]] UiFragment fragment_of(const UiVertex& vertex) noexcept;
// Linear interpolation of two vertices' fragment inputs (`t` from a toward b); the kind is `a`'s.
[[nodiscard]] UiFragment mix_fragments(const UiFragment& a, const UiFragment& b, float t) noexcept;

// The signed distance `box` above.
[[nodiscard]] float box_distance(glm::vec2 p, glm::vec2 half_size, glm::vec2 radii) noexcept;
[[nodiscard]] float clip_coverage(const UiFragment& fragment) noexcept;
// The texel the kind reads, or nothing for the analytic kinds (Fill, Stroke, Line, Arc).
[[nodiscard]] std::optional<UiTexelFetch> fragment_fetch(const UiFragment& fragment) noexcept;
// The premultiplied output color, given the raw texel fragment_fetch asked for (R8 pages: coverage in .r; ignored by
// the analytic kinds).
[[nodiscard]] glm::vec4 shade_fragment(const UiFragment& fragment, glm::vec4 texel = glm::vec4(1.0f)) noexcept;

}
