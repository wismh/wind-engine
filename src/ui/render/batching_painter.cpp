#include "ui/render/batching_painter.h"

#include "ui/render/nine_slice.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace engine::ui {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// A clip extent no vertex reaches: "not clipped".
constexpr float kNoClip = 1.0e7f;
// Maps every point to uv (0, 0), for the kinds that read no uv.
constexpr Affine2 kNoUv{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

[[nodiscard]] render::Rect normalized(const render::Rect& rect) noexcept {
    render::Rect r = rect;
    if (r.w < 0.0f) {
        r.x += r.w;
        r.w = -r.w;
    }
    if (r.h < 0.0f) {
        r.y += r.h;
        r.h = -r.h;
    }
    return r;
}

// nvgRoundedRect's corner radii: none under 0.1 (nvgRect), else clamped to half of each side separately.
[[nodiscard]] glm::vec2 corner_radii(const render::Rect& rect, float radius) noexcept {
    if (radius < 0.1f) {
        return {0.0f, 0.0f};
    }
    return {std::min(radius, std::abs(rect.w) * 0.5f), std::min(radius, std::abs(rect.h) * 0.5f)};
}

// Local points to (point - origin) / extent: the coordinates of nvgImagePattern(origin, extent).
[[nodiscard]] Affine2 pattern_map(glm::vec2 origin, glm::vec2 extent) noexcept {
    return {1.0f / extent.x, 0.0f, 0.0f, 1.0f / extent.y, -origin.x / extent.x, -origin.y / extent.y};
}

}

BatchingPainter::BatchingPainter(PaintResources& resources) : resources_(resources) {}

void BatchingPainter::begin_frame(float width, float height, float pixel_ratio) {
    list_.clear();
    stack_.clear();
    state_ = State{};
    width_ = width;
    height_ = height;
    pixel_ratio_ = pixel_ratio > 0.0f ? pixel_ratio : 1.0f;
    resources_.begin_list();
}

void BatchingPainter::end_frame() {}

void BatchingPainter::save() {
    stack_.push_back(state_);
}

void BatchingPainter::restore() {
    if (stack_.empty()) {
        return;
    }
    state_ = stack_.back();
    stack_.pop_back();
}

void BatchingPainter::set_clip(const render::Rect& rect) {
    // nvgScissor.
    const float w = std::max(0.0f, rect.w);
    const float h = std::max(0.0f, rect.h);
    State& s = state_;
    s.clipped = true;
    s.clip_xform = compose(s.xform, Affine2::translation({rect.x + w * 0.5f, rect.y + h * 0.5f}));
    s.clip_extent = {w * 0.5f, h * 0.5f};
    // Each clip axis's length in logical units, then px.
    const Affine2& c = s.clip_xform;
    const float kx = std::sqrt(c.a * c.a + c.c * c.c) * pixel_ratio_;
    const float ky = std::sqrt(c.b * c.b + c.d * c.d) * pixel_ratio_;
    const Affine2 inv = c.inverse();
    s.to_clip_px = {inv.a * kx, inv.b * ky, inv.c * kx, inv.d * ky, inv.e * kx, inv.f * ky};
    s.clip_extent_px = {s.clip_extent.x * kx, s.clip_extent.y * ky};
    const glm::vec2 half{s.clip_extent.x * std::abs(c.a) + s.clip_extent.y * std::abs(c.c),
            s.clip_extent.x * std::abs(c.b) + s.clip_extent.y * std::abs(c.d)};
    s.clip_min = glm::vec2(c.e, c.f) - half;
    s.clip_max = glm::vec2(c.e, c.f) + half;
}

void BatchingPainter::scissor(const render::Rect& rect) {
    if (!state_.clipped) {
        set_clip(rect);
        return;
    }
    // nvgIntersectScissor: the current clip's bounding box in the current transform's frame, intersected with `rect`.
    // Exact while the transform's rotation has not changed since the outer clip; an approximation otherwise.
    const Affine2 p = compose(state_.xform.inverse(), state_.clip_xform);
    const float tex = state_.clip_extent.x * std::abs(p.a) + state_.clip_extent.y * std::abs(p.c);
    const float tey = state_.clip_extent.x * std::abs(p.b) + state_.clip_extent.y * std::abs(p.d);
    const float min_x = std::max(p.e - tex, rect.x);
    const float min_y = std::max(p.f - tey, rect.y);
    const float max_x = std::min(p.e + tex, rect.x + rect.w);
    const float max_y = std::min(p.f + tey, rect.y + rect.h);
    set_clip({min_x, min_y, std::max(0.0f, max_x - min_x), std::max(0.0f, max_y - min_y)});
}

void BatchingPainter::apply_transform(glm::vec2 center, float rotation_radians, float scale) {
    Affine2& x = state_.xform;
    x = compose(x, Affine2::translation(center));
    x = compose(x, Affine2::rotation(rotation_radians));
    x = compose(x, Affine2::scaling(scale));
    x = compose(x, Affine2::translation(-center));
}

void BatchingPainter::apply_view(glm::vec2 origin, glm::vec2 pan, float zoom) {
    Affine2& x = state_.xform;
    x = compose(x, Affine2::translation(origin));
    x = compose(x, Affine2::scaling(zoom));
    x = compose(x, Affine2::translation(pan));
    x = compose(x, Affine2::translation(-origin));
}

void BatchingPainter::set_opacity(float opacity) {
    state_.alpha = std::clamp(opacity, 0.0f, 1.0f);
}

float BatchingPainter::px_scale() const noexcept {
    return state_.xform.average_scale() * pixel_ratio_;
}

float BatchingPainter::stroke_half_width(float width, float& alpha) const noexcept {
    // nvgStroke: the width in logical units, capped at 200; under one device pixel it draws one pixel wide and
    // fades by the square of the lost width (coverage is area).
    const float fringe = 1.0f / pixel_ratio_;
    float stroke = std::clamp(width * state_.xform.average_scale(), 0.0f, 200.0f);
    if (stroke < fringe) {
        const float a = std::clamp(stroke / fringe, 0.0f, 1.0f);
        alpha *= a * a;
        stroke = fringe;
    }
    return stroke * pixel_ratio_ * 0.5f;
}

void BatchingPainter::push(Quad& quad) {
    if (state_.clipped) {
        glm::vec2 lo = quad.position[0];
        glm::vec2 hi = quad.position[0];
        for (int i = 1; i < 4; ++i) {
            lo = glm::min(lo, quad.position[i]);
            hi = glm::max(hi, quad.position[i]);
        }
        // The scissor ramp reaches half a device pixel past the edge.
        const float margin = 1.0f / pixel_ratio_;
        if (hi.x < state_.clip_min.x - margin || hi.y < state_.clip_min.y - margin ||
                lo.x > state_.clip_max.x + margin || lo.y > state_.clip_max.y + margin) {
            return;
        }
    }
    if (quad.textured) {
        list_.bind(quad.slot, quad.page);
    }
    UiVertex* v = list_.push_quad();
    for (int i = 0; i < 4; ++i) {
        v[i].position = quad.position[i];
        v[i].uv = quad.uv[i];
        v[i].local = quad.local[i];
        v[i].shape = quad.shape;
        if (state_.clipped) {
            const glm::vec2 c = state_.to_clip_px.apply(quad.position[i]);
            v[i].clip = {c.x, c.y, state_.clip_extent_px.x, state_.clip_extent_px.y};
        } else {
            v[i].clip = {0.0f, 0.0f, kNoClip, kNoClip};
        }
        v[i].aux = quad.aux;
        v[i].color = quad.color;
        v[i].kind = quad.kind;
    }
}

void BatchingPainter::push_box(const render::Rect& rect, float grow, const Affine2& uv_map, Quad& quad) {
    const float s = px_scale();
    const render::Rect r = normalized(rect);
    const glm::vec2 half{r.w * 0.5f, r.h * 0.5f};
    const glm::vec2 center{r.x + half.x, r.y + half.y};
    const glm::vec2 reach = half + glm::vec2(grow + 1.0f / s);
    const glm::vec2 offsets[4] = {{-reach.x, -reach.y}, {reach.x, -reach.y}, {reach.x, reach.y}, {-reach.x, reach.y}};
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 p = center + offsets[i];
        quad.position[i] = state_.xform.apply(p);
        quad.local[i] = offsets[i] * s;
        quad.uv[i] = uv_map.apply(p);
    }
    quad.shape.x = half.x * s;
    quad.shape.y = half.y * s;
    push(quad);
}

void BatchingPainter::fill_rounded_rect(const render::Rect& rect, float radius, glm::vec4 color) {
    const float s = px_scale();
    if (rect.w == 0.0f || rect.h == 0.0f || !(s > 0.0f) || !(color.a * state_.alpha > 0.0f)) {
        return;
    }
    Quad quad;
    quad.kind = UiShapeKind::Fill;
    quad.color = pack_premultiplied(color, state_.alpha);
    const glm::vec2 radii = corner_radii(rect, radius) * s;
    quad.shape = {0.0f, 0.0f, radii.x, radii.y};
    push_box(rect, 0.0f, kNoUv, quad);
}

void BatchingPainter::fill_rounded_rect_gradient(const render::Rect& rect, float radius, const Gradient& gradient) {
    const float s = px_scale();
    if (gradient.stops.size() < 2 || rect.w == 0.0f || rect.h == 0.0f || !(s > 0.0f) || !(state_.alpha > 0.0f)) {
        return;
    }
    Quad quad;
    const glm::vec2 center{rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
    Affine2 uv_map = kNoUv;
    switch (gradient.kind) {
        case GradientKind::Linear: {
            // Linear gradient from the end points: t = (dot(p - start, dir) - d / 2) / feather + 1/2, feather =
            // max(1, d), d the distance between the end points.
            const float angle = gradient.angle_deg * (kPi / 180.0f);
            glm::vec2 dir{std::sin(angle), -std::cos(angle)};
            const float half_len = 0.5f * (std::abs(rect.w * dir.x) + std::abs(rect.h * dir.y));
            const glm::vec2 start = center - dir * half_len;
            const glm::vec2 end = center + dir * half_len;
            const float d = glm::length(end - start);
            if (d > 0.0001f) {
                dir = (end - start) / d;
            } else {
                dir = {0.0f, 1.0f};
            }
            const float feather = std::max(1.0f, d);
            uv_map = {dir.x / feather, 0.0f, dir.y / feather, 0.0f,
                    -glm::dot(start, dir) / feather - d * 0.5f / feather + 0.5f, 0.0f};
            quad.kind = UiShapeKind::LinearGradient;
            break;
        }
        case GradientKind::Radial: {
            // nvgRadialGradient(inner 0, outer R): t = (|p - c| - R / 2) / feather + 1/2, feather = max(1, R).
            const float outer = 0.5f * std::max(rect.w, rect.h);
            const float feather = std::max(1.0f, outer);
            uv_map = pattern_map(center, glm::vec2(feather));
            quad.aux.y = 0.5f - outer * 0.5f / feather;
            quad.kind = UiShapeKind::RadialGradient;
            break;
        }
        case GradientKind::Conic:
            // The conic texture stretched over the rect: the angle is taken in the rect's normalized frame.
            uv_map = pattern_map(center, glm::vec2(rect.w, rect.h));
            quad.kind = UiShapeKind::ConicGradient;
            break;
    }
    const RampRow row = resources_.ramps.row(gradient);
    quad.textured = true;
    quad.slot = UiTextureSlot::Ramp;
    quad.page = row.page;
    quad.aux.x = row.v;
    quad.color = pack_premultiplied(glm::vec4(1.0f), state_.alpha);
    const glm::vec2 radii = corner_radii(rect, radius) * s;
    quad.shape = {0.0f, 0.0f, radii.x, radii.y};
    push_box(rect, 0.0f, uv_map, quad);
}

void BatchingPainter::stroke_rounded_rect(const render::Rect& rect, float radius, float width, glm::vec4 color) {
    const float s = px_scale();
    if ((rect.w == 0.0f && rect.h == 0.0f) || !(width > 0.0f) || !(s > 0.0f)) {
        return;
    }
    float alpha = state_.alpha;
    const float half_width = stroke_half_width(width, alpha);
    if (!(color.a * alpha > 0.0f)) {
        return;
    }
    Quad quad;
    quad.kind = UiShapeKind::Stroke;
    quad.color = pack_premultiplied(color, alpha);
    const glm::vec2 radii = corner_radii(rect, radius) * s;
    quad.shape = {0.0f, 0.0f, radii.x, radii.y};
    quad.aux.x = half_width;
    push_box(rect, half_width / s, kNoUv, quad);
}

void BatchingPainter::draw_line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) {
    const float s = px_scale();
    const float length = glm::length(to - from);
    if (!(length > 0.0f) || !(width > 0.0f) || !(s > 0.0f)) {
        return;
    }
    float alpha = state_.alpha;
    const float half_width = stroke_half_width(width, alpha);
    if (!(color.a * alpha > 0.0f)) {
        return;
    }
    const glm::vec2 dir = (to - from) / length;
    const glm::vec2 normal{-dir.y, dir.x};
    const glm::vec2 mid = (from + to) * 0.5f;
    const float margin = 1.0f / s;
    const float along = length * 0.5f + margin;
    const float across = half_width / s + margin;
    const glm::vec2 offsets[4] = {{-along, -across}, {along, -across}, {along, across}, {-along, across}};
    Quad quad;
    for (int i = 0; i < 4; ++i) {
        quad.position[i] = state_.xform.apply(mid + dir * offsets[i].x + normal * offsets[i].y);
        quad.local[i] = offsets[i] * s;
        quad.uv[i] = {};
    }
    quad.kind = UiShapeKind::Line;
    quad.color = pack_premultiplied(color, alpha);
    quad.shape = {length * 0.5f * s, half_width, 0.0f, 0.0f};
    push(quad);
}

void BatchingPainter::stroke_arc(glm::vec2 center, float radius, float start_angle, float end_angle, float width,
        glm::vec4 color) {
    const float s = px_scale();
    if (!(radius > 0.0f) || !(width > 0.0f) || !(s > 0.0f)) {
        return;
    }
    // A clockwise sweep normalized into (0, 2pi], a full turn at most.
    float sweep = end_angle - start_angle;
    if (std::abs(sweep) >= 2.0f * kPi) {
        sweep = 2.0f * kPi;
    } else {
        while (sweep < 0.0f) {
            sweep += 2.0f * kPi;
        }
    }
    float alpha = state_.alpha;
    const float half_width = stroke_half_width(width, alpha);
    if (!(sweep > 0.0f) || !(color.a * alpha > 0.0f)) {
        return;
    }
    // IDrawList::arc's angles: 0 at 12 o'clock, clockwise (y down), so the direction of angle a is (sin a, -cos a).
    const float mid = start_angle + sweep * 0.5f;
    const glm::vec2 axis{std::sin(mid), -std::cos(mid)};
    const glm::vec2 side{std::cos(mid), std::sin(mid)};
    const float reach = radius + half_width / s + 1.0f / s;
    const glm::vec2 offsets[4] = {{-reach, -reach}, {reach, -reach}, {reach, reach}, {-reach, reach}};
    Quad quad;
    for (int i = 0; i < 4; ++i) {
        quad.position[i] = state_.xform.apply(center + side * offsets[i].x + axis * offsets[i].y);
        quad.local[i] = offsets[i] * s;
        quad.uv[i] = {};
    }
    quad.kind = UiShapeKind::Arc;
    quad.color = pack_premultiplied(color, alpha);
    quad.shape = {radius * s, half_width, sweep * 0.5f, 0.0f};
    push(quad);
}

void BatchingPainter::fill_path(std::span<const PathSegment> path, glm::vec4 color) {
    if (path.empty() || !(color.a * state_.alpha > 0.0f)) {
        return;
    }
    // Rasterized in device pixels under the transform's rotation and scale, at the quarter-pixel phase nearest its
    // translation; the quad then sits at the exact translation (the residual, an eighth of a pixel at most, is
    // resampled bilinearly).
    const Affine2& x = state_.xform;
    const float r = pixel_ratio_;
    const Affine2 linear{x.a * r, x.b * r, x.c * r, x.d * r, 0.0f, 0.0f};
    const glm::vec2 translation{x.e * r, x.f * r};
    const glm::vec2 snapped = glm::round(translation * MaskAtlas::kPhaseSteps) / MaskAtlas::kPhaseSteps;
    const glm::vec2 whole = glm::floor(snapped);
    const AtlasMask* mask = resources_.masks.mask(path, linear, snapped - whole, 0.25f * r);
    if (mask == nullptr) {
        return;
    }
    const AtlasPage& page = resources_.masks.page(mask->page);
    const glm::vec2 page_size{static_cast<float>(page.width()), static_cast<float>(page.height())};
    // The cell minus its outer ring of zeros, so a bilinear fetch at the quad's edge reads this cell's own zeros.
    const glm::vec2 cell_min{static_cast<float>(mask->cell.x0 + 1), static_cast<float>(mask->cell.y0 + 1)};
    const glm::vec2 cell_max{static_cast<float>(mask->cell.x1 - 1), static_cast<float>(mask->cell.y1 - 1)};
    const glm::vec2 device_min = mask->origin + glm::vec2(1.0f) + whole + (translation - snapped);
    const glm::vec2 device_max = device_min + (cell_max - cell_min);
    Quad quad;
    const glm::vec2 corners[4] = {device_min, {device_max.x, device_min.y}, device_max, {device_min.x, device_max.y}};
    const glm::vec2 uvs[4] = {cell_min, {cell_max.x, cell_min.y}, cell_max, {cell_min.x, cell_max.y}};
    for (int i = 0; i < 4; ++i) {
        quad.position[i] = corners[i] / r;
        quad.local[i] = {};
        quad.uv[i] = uvs[i] / page_size;
    }
    quad.kind = UiShapeKind::Mask;
    quad.color = pack_premultiplied(color, state_.alpha);
    quad.textured = true;
    quad.slot = UiTextureSlot::Mask;
    quad.page = mask->page;
    push(quad);
}

void BatchingPainter::stroke_path(std::span<const PathSegment> path, float width, glm::vec4 color) {
    if (path.empty() || !(width > 0.0f) || !(color.a * state_.alpha > 0.0f)) {
        return;
    }
    float alpha = state_.alpha;
    const float half = stroke_half_width(width, alpha);
    if (!(color.a * alpha > 0.0f) || !(half > 0.0f)) {
        return;
    }
    const Affine2& x = state_.xform;
    const float r = pixel_ratio_;
    const Affine2 linear{x.a * r, x.b * r, x.c * r, x.d * r, 0.0f, 0.0f};
    const glm::vec2 translation{x.e * r, x.f * r};
    const glm::vec2 snapped = glm::round(translation * MaskAtlas::kPhaseSteps) / MaskAtlas::kPhaseSteps;
    const glm::vec2 whole = glm::floor(snapped);
    const AtlasMask* mask = resources_.masks.stroke_mask(path, linear, snapped - whole, half * 2.0f, 0.25f * r);
    if (mask == nullptr) {
        return;
    }
    const AtlasPage& page = resources_.masks.page(mask->page);
    const glm::vec2 page_size{static_cast<float>(page.width()), static_cast<float>(page.height())};
    const glm::vec2 cell_min{static_cast<float>(mask->cell.x0 + 1), static_cast<float>(mask->cell.y0 + 1)};
    const glm::vec2 cell_max{static_cast<float>(mask->cell.x1 - 1), static_cast<float>(mask->cell.y1 - 1)};
    const glm::vec2 device_min = mask->origin + glm::vec2(1.0f) + whole + (translation - snapped);
    const glm::vec2 device_max = device_min + (cell_max - cell_min);
    Quad quad;
    const glm::vec2 corners[4] = {device_min, {device_max.x, device_min.y}, device_max, {device_min.x, device_max.y}};
    const glm::vec2 uvs[4] = {cell_min, {cell_max.x, cell_min.y}, cell_max, {cell_min.x, cell_max.y}};
    for (int i = 0; i < 4; ++i) {
        quad.position[i] = corners[i] / r;
        quad.local[i] = {};
        quad.uv[i] = uvs[i] / page_size;
    }
    quad.kind = UiShapeKind::Mask;
    quad.color = pack_premultiplied(color, alpha);
    quad.textured = true;
    quad.slot = UiTextureSlot::Mask;
    quad.page = mask->page;
    push(quad);
}

void BatchingPainter::set_font(AssetId font, float size) {
    state_.font = font;
    state_.font_size = size;
}

void BatchingPainter::fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, UiAlign horizontal,
        UiAlign vertical) {
    if (text.empty() || !(color.a * state_.alpha > 0.0f)) {
        return;
    }
    const Affine2& x = state_.xform;
    const float raster_scale = text_raster_scale(x.average_scale(), pixel_ratio_);
    glyphs_.clear();
    resources_.fonts.layout_text(text, state_.font, state_.font_size, position, horizontal, vertical, raster_scale,
            glyphs_);
    const bool flipped = x.determinant() < 0.0f;
    const std::uint32_t packed = pack_premultiplied(color, state_.alpha);
    for (GlyphQuad g : glyphs_) {
        if (flipped) {
            // A mirroring transform swaps the quad's rows and their texture rows.
            std::swap(g.min.y, g.max.y);
            std::swap(g.uv_min.y, g.uv_max.y);
        }
        Quad quad;
        const glm::vec2 corners[4] = {g.min, {g.max.x, g.min.y}, g.max, {g.min.x, g.max.y}};
        const glm::vec2 uvs[4] = {g.uv_min, {g.uv_max.x, g.uv_min.y}, g.uv_max, {g.uv_min.x, g.uv_max.y}};
        for (int i = 0; i < 4; ++i) {
            quad.position[i] = x.apply(corners[i]);
            quad.local[i] = {};
            quad.uv[i] = uvs[i];
        }
        quad.kind = UiShapeKind::Glyph;
        quad.color = packed;
        quad.textured = true;
        quad.slot = UiTextureSlot::Glyph;
        quad.page = g.page;
        push(quad);
    }
}

BatchingPainter::Quad BatchingPainter::image_quad(const AtlasImage& img, UiShapeKind kind) const {
    Quad quad;
    quad.kind = kind;
    quad.color = pack_premultiplied(glm::vec4(1.0f), state_.alpha);
    quad.shape = {0.0f, 0.0f, img.uv_min.x, img.uv_min.y};
    quad.aux = img.uv_size;
    quad.textured = true;
    quad.slot = UiTextureSlot::Image;
    quad.page = img.page;
    return quad;
}

void BatchingPainter::image(AssetId texture, const render::Rect& rect) {
    const AtlasImage* img = resources_.images.find(texture);
    if (img == nullptr || rect.w == 0.0f || rect.h == 0.0f || !(px_scale() > 0.0f) || !(state_.alpha > 0.0f)) {
        return;
    }
    Quad quad = image_quad(*img, UiShapeKind::Image);
    push_box(rect, 0.0f, pattern_map({rect.x, rect.y}, {rect.w, rect.h}), quad);
}

void BatchingPainter::image_repeat(AssetId texture, const render::Rect& rect) {
    const AtlasImage* img = resources_.images.find(texture);
    if (img == nullptr || rect.w == 0.0f || rect.h == 0.0f || !(px_scale() > 0.0f) || !(state_.alpha > 0.0f)) {
        return;
    }
    Quad quad = image_quad(*img, UiShapeKind::ImageRepeat);
    // The texture's own pixel size, anchored at the rect's origin.
    const glm::vec2 tile{static_cast<float>(img->width), static_cast<float>(img->height)};
    push_box(rect, 0.0f, pattern_map({rect.x, rect.y}, tile), quad);
}

void BatchingPainter::image_nine_slice(AssetId texture, const render::Rect& rect, const BoxInsets& insets) {
    const AtlasImage* img = resources_.images.find(texture);
    if (img == nullptr || rect.w <= 0.0f || rect.h <= 0.0f || !(px_scale() > 0.0f) || !(state_.alpha > 0.0f)) {
        return;
    }
    const NineSlicePieces pieces = nine_slice_pieces(
            rect, insets, {static_cast<float>(img->width), static_cast<float>(img->height)});
    for (std::size_t i = 0; i < pieces.count; ++i) {
        const NineSlicePiece& piece = pieces.pieces[i];
        Quad quad = image_quad(*img, UiShapeKind::Image);
        push_box(piece.rect, 0.0f, pattern_map(piece.pattern_origin, piece.pattern_extent), quad);
    }
}

glm::vec2 BatchingPainter::measure_text(std::string_view text, AssetId font, float size) {
    return resources_.fonts.measure_text(text, font, size);
}

TextFontMetrics BatchingPainter::font_metrics(AssetId font, float size) {
    return resources_.fonts.font_metrics(font, size);
}

TextBlock BatchingPainter::break_lines(std::string_view text, AssetId font, float size, float max_width) {
    return resources_.fonts.break_lines(text, font, size, max_width);
}

}
