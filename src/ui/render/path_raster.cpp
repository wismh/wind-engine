#include "ui/render/path_raster.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::ui {
namespace {

// nvg__tesselateBezier's recursion limit.
constexpr int kMaxTessellationLevel = 10;
// NanoVG's distTol, fringeWidth, and nvgFill's miter limit, in device pixels.
constexpr float kDistanceTolerance = 0.01f;
constexpr float kFringe = 1.0f;
constexpr float kMiterLimit = 2.4f;
// NVG_PT_CORNER, NVG_PT_LEFT, NVG_PT_BEVEL, NVG_PR_INNERBEVEL.
constexpr std::uint8_t kCorner = 0x01;
constexpr std::uint8_t kLeft = 0x02;
constexpr std::uint8_t kBevel = 0x04;
constexpr std::uint8_t kInnerBevel = 0x08;

[[nodiscard]] float cross(glm::vec2 a, glm::vec2 b) noexcept {
    return a.x * b.y - a.y * b.x;
}

[[nodiscard]] bool same_point(glm::vec2 a, glm::vec2 b) noexcept {
    const glm::vec2 d = b - a;
    return d.x * d.x + d.y * d.y < kDistanceTolerance * kDistanceTolerance;
}

// nvg__normalize.
float normalize(glm::vec2& v) noexcept {
    const float length = std::sqrt(v.x * v.x + v.y * v.y);
    if (length > 1e-6f) {
        v *= 1.0f / length;
    }
    return length;
}

// The left normal of a direction (NanoVG's dlx, dly).
[[nodiscard]] glm::vec2 left_normal(glm::vec2 d) noexcept {
    return {d.y, -d.x};
}

}

void PathRasterizer::add_point(glm::vec2 p, std::uint8_t flags) {
    // nvg__addPoint: a point on top of the last one only adds its flags.
    Contour& contour = contours_.back();
    if (contour.count > 0 && same_point(points_.back().p, p)) {
        points_.back().flags |= flags;
        return;
    }
    points_.push_back({p, {}, 0.0f, {}, flags});
    contour.count += 1;
}

void PathRasterizer::tessellate(glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, glm::vec2 p4, int level,
        std::uint8_t flags) {
    // nvg__tesselateBezier, point for point: only the curve's end point is a corner.
    if (level > kMaxTessellationLevel) {
        return;
    }
    const glm::vec2 p12 = (p1 + p2) * 0.5f;
    const glm::vec2 p23 = (p2 + p3) * 0.5f;
    const glm::vec2 p34 = (p3 + p4) * 0.5f;
    const glm::vec2 p123 = (p12 + p23) * 0.5f;
    const float dx = p4.x - p1.x;
    const float dy = p4.y - p1.y;
    const float d2 = std::abs((p2.x - p4.x) * dy - (p2.y - p4.y) * dx);
    const float d3 = std::abs((p3.x - p4.x) * dy - (p3.y - p4.y) * dx);
    if ((d2 + d3) * (d2 + d3) < tolerance_ * (dx * dx + dy * dy)) {
        add_point(p4, flags);
        return;
    }
    const glm::vec2 p234 = (p23 + p34) * 0.5f;
    const glm::vec2 p1234 = (p123 + p234) * 0.5f;
    tessellate(p1, p12, p123, p1234, level + 1, 0);
    tessellate(p1234, p234, p34, p4, level + 1, flags);
}

bool PathRasterizer::flatten(std::span<const PathSegment> path, const Affine2& to_raster, float tolerance) {
    tolerance_ = tolerance;
    points_.clear();
    contours_.clear();
    fill_.clear();
    fringe_.clear();
    // Solid or hole is decided on the points and control points, as the NanoVG painter's fill_path did.
    float reference = 0.0f;
    glm::vec2 control_start{};
    glm::vec2 control_previous{};
    float control_area = 0.0f;
    const auto visit_control = [&](glm::vec2 point) {
        control_area += control_previous.x * point.y - point.x * control_previous.y;
        control_previous = point;
    };
    const auto close_contour = [&] {
        if (contours_.empty()) {
            return;
        }
        control_area += control_previous.x * control_start.y - control_start.x * control_previous.y;
        contours_.back().control_area = control_area;
        if (std::abs(control_area) > std::abs(reference)) {
            reference = control_area;
        }
    };
    glm::vec2 last{};
    for (const PathSegment& segment : path) {
        const glm::vec2 p = to_raster.apply(segment.p);
        switch (segment.kind) {
            case PathSegment::Kind::Move:
                close_contour();
                contours_.push_back({static_cast<std::uint32_t>(points_.size())});
                control_start = control_previous = p;
                control_area = 0.0f;
                add_point(p, kCorner);
                break;
            case PathSegment::Kind::Line:
                if (contours_.empty()) {
                    break;
                }
                visit_control(p);
                add_point(p, kCorner);
                break;
            case PathSegment::Kind::Quad: {
                if (contours_.empty()) {
                    break;
                }
                // nvgQuadTo: the cubic with control points two thirds of the way to the quadratic's.
                const glm::vec2 c = to_raster.apply(segment.c1);
                visit_control(c);
                visit_control(p);
                tessellate(points_.back().p, last + (c - last) * (2.0f / 3.0f), p + (c - p) * (2.0f / 3.0f), p, 0,
                        kCorner);
                break;
            }
            case PathSegment::Kind::Cubic: {
                if (contours_.empty()) {
                    break;
                }
                const glm::vec2 c1 = to_raster.apply(segment.c1);
                const glm::vec2 c2 = to_raster.apply(segment.c2);
                visit_control(c1);
                visit_control(c2);
                visit_control(p);
                tessellate(points_.back().p, c1, c2, p, 0, kCorner);
                break;
            }
        }
        if (!contours_.empty()) {
            last = p;
        }
    }
    close_contour();

    // nvg__flattenPaths: drop a closing point on top of the first, force the winding, and measure the segments.
    glm::vec2 points_min(std::numeric_limits<float>::max());
    glm::vec2 points_max(std::numeric_limits<float>::lowest());
    bool any = false;
    for (Contour& contour : contours_) {
        Point* const pts = points_.data() + contour.first;
        if (contour.count > 0 && same_point(pts[contour.count - 1].p, pts[0].p)) {
            contour.count -= 1;
        }
        if (contour.count > 2) {
            // nvg__polyArea: positive for NVG_CCW (solid), which a hole must not be.
            float area = 0.0f;
            for (std::uint32_t i = 2; i < contour.count; ++i) {
                area += cross(pts[i].p - pts[0].p, pts[i - 1].p - pts[0].p);
            }
            const bool solid = contour.control_area * reference >= 0.0f;
            if ((solid && area < 0.0f) || (!solid && area > 0.0f)) {
                std::reverse(pts, pts + contour.count);
            }
        }
        for (std::uint32_t i = 0; i < contour.count; ++i) {
            Point& p0 = pts[i];
            p0.d = pts[(i + 1) % contour.count].p - p0.p;
            p0.len = normalize(p0.d);
            points_min = glm::min(points_min, p0.p);
            points_max = glm::max(points_max, p0.p);
        }
        any = any || contour.count >= 2;
    }
    if (!any || !(points_max.x > points_min.x && points_max.y > points_min.y)) {
        min_ = max_ = glm::vec2(0.0f);
        return false;
    }

    calculate_joins();
    convex_ = contours_.size() == 1 && contours_.front().convex;
    expand_fill();

    min_ = points_min;
    max_ = points_max;
    for (const glm::vec2 p : fill_) {
        min_ = glm::min(min_, p);
        max_ = glm::max(max_, p);
    }
    for (const FringeVertex& v : fringe_) {
        min_ = glm::min(min_, v.p);
        max_ = glm::max(max_, v.p);
    }
    return true;
}

void PathRasterizer::calculate_joins() {
    // nvg__calculateJoins(ctx, fringeWidth, NVG_MITER, 2.4).
    constexpr float kInverseWidth = 1.0f / kFringe;
    for (Contour& contour : contours_) {
        Point* const pts = points_.data() + contour.first;
        std::uint32_t left_turns = 0;
        for (std::uint32_t j = 0; j < contour.count; ++j) {
            const Point& p0 = pts[(j + contour.count - 1) % contour.count];
            Point& p1 = pts[j];
            p1.dm = (left_normal(p0.d) + left_normal(p1.d)) * 0.5f;
            const float dmr2 = p1.dm.x * p1.dm.x + p1.dm.y * p1.dm.y;
            if (dmr2 > 0.000001f) {
                p1.dm *= std::min(1.0f / dmr2, 600.0f);
            }
            p1.flags = (p1.flags & kCorner) != 0 ? kCorner : 0;
            if (p1.d.x * p0.d.y - p0.d.x * p1.d.y > 0.0f) {
                ++left_turns;
                p1.flags |= kLeft;
            }
            const float limit = std::max(1.01f, std::min(p0.len, p1.len) * kInverseWidth);
            if (dmr2 * limit * limit < 1.0f) {
                p1.flags |= kInnerBevel;
            }
            if ((p1.flags & kCorner) != 0 && dmr2 * kMiterLimit * kMiterLimit < 1.0f) {
                p1.flags |= kBevel;
            }
        }
        contour.convex = left_turns == contour.count;
    }
}

void PathRasterizer::bevel_join(const Point& p0, const Point& p1, float lw, float rw, float lu, float ru) {
    // nvg__bevelJoin with nvg__chooseBevel.
    const glm::vec2 dl0 = left_normal(p0.d);
    const glm::vec2 dl1 = left_normal(p1.d);
    const bool inner_bevel = (p1.flags & kInnerBevel) != 0;
    if ((p1.flags & kLeft) != 0) {
        const glm::vec2 l0 = inner_bevel ? p1.p + dl0 * lw : p1.p + p1.dm * lw;
        const glm::vec2 l1 = inner_bevel ? p1.p + dl1 * lw : p1.p + p1.dm * lw;
        fringe(l0, lu);
        fringe(p1.p - dl0 * rw, ru);
        if ((p1.flags & kBevel) != 0) {
            fringe(l0, lu);
            fringe(p1.p - dl0 * rw, ru);
            fringe(l1, lu);
            fringe(p1.p - dl1 * rw, ru);
        } else {
            const glm::vec2 r0 = p1.p - p1.dm * rw;
            fringe(p1.p, 0.5f);
            fringe(p1.p - dl0 * rw, ru);
            fringe(r0, ru);
            fringe(r0, ru);
            fringe(p1.p, 0.5f);
            fringe(p1.p - dl1 * rw, ru);
        }
        fringe(l1, lu);
        fringe(p1.p - dl1 * rw, ru);
    } else {
        const glm::vec2 r0 = inner_bevel ? p1.p - dl0 * rw : p1.p - p1.dm * rw;
        const glm::vec2 r1 = inner_bevel ? p1.p - dl1 * rw : p1.p - p1.dm * rw;
        fringe(p1.p + dl0 * lw, lu);
        fringe(r0, ru);
        if ((p1.flags & kBevel) != 0) {
            fringe(p1.p + dl0 * lw, lu);
            fringe(r0, ru);
            fringe(p1.p + dl1 * lw, lu);
            fringe(r1, ru);
        } else {
            const glm::vec2 l0 = p1.p + p1.dm * lw;
            fringe(p1.p + dl0 * lw, lu);
            fringe(p1.p, 0.5f);
            fringe(l0, lu);
            fringe(l0, lu);
            fringe(p1.p + dl1 * lw, lu);
            fringe(p1.p, 0.5f);
        }
        fringe(p1.p + dl1 * lw, lu);
        fringe(r1, ru);
    }
}

void PathRasterizer::expand_fill() {
    // nvg__expandFill(ctx, fringeWidth, NVG_MITER, 2.4) with antialiasing on.
    constexpr float woff = 0.5f * kFringe;
    for (Contour& contour : contours_) {
        const Point* const pts = points_.data() + contour.first;
        contour.fill_first = static_cast<std::uint32_t>(fill_.size());
        for (std::uint32_t j = 0; j < contour.count; ++j) {
            const Point& p0 = pts[(j + contour.count - 1) % contour.count];
            const Point& p1 = pts[j];
            if ((p1.flags & kBevel) != 0 && (p1.flags & kLeft) == 0) {
                fill_.push_back(p1.p + left_normal(p0.d) * woff);
                fill_.push_back(p1.p + left_normal(p1.d) * woff);
            } else {
                fill_.push_back(p1.p + p1.dm * woff);
            }
        }
        contour.fill_count = static_cast<std::uint32_t>(fill_.size()) - contour.fill_first;

        float lw = kFringe + woff;
        const float rw = kFringe - woff;
        float lu = 0.0f;
        const float ru = 1.0f;
        if (convex_) {
            // Half a fringe: the convex fill draws without the stencil.
            lw = woff;
            lu = 0.5f;
        }
        contour.fringe_first = static_cast<std::uint32_t>(fringe_.size());
        for (std::uint32_t j = 0; j < contour.count; ++j) {
            const Point& p0 = pts[(j + contour.count - 1) % contour.count];
            const Point& p1 = pts[j];
            if ((p1.flags & (kBevel | kInnerBevel)) != 0) {
                bevel_join(p0, p1, lw, rw, lu, ru);
            } else {
                fringe(p1.p + p1.dm * lw, lu);
                fringe(p1.p - p1.dm * rw, ru);
            }
        }
        if (contour.count > 0) {
            const glm::vec2 first = fringe_[contour.fringe_first].p;
            const glm::vec2 second = fringe_[contour.fringe_first + 1].p;
            fringe(first, lu);
            fringe(second, ru);
        }
        contour.fringe_count = static_cast<std::uint32_t>(fringe_.size()) - contour.fringe_first;
    }
}

void PathRasterizer::rasterize(glm::vec2 offset, int width, int height, std::uint8_t* out, int stride) {
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t row_cells = static_cast<std::size_t>(width) + 1;
    winding_.assign(row_cells * static_cast<std::size_t>(height), 0);
    coverage_.assign(count, 0.0f);

    // The stencil pass: the winding number of the inset contours at every pixel center. An edge adds its direction
    // at the first center on or right of where it crosses the row's center line; rows take centers in [top, bottom).
    for (const Contour& contour : contours_) {
        for (std::uint32_t i = 0; i < contour.fill_count; ++i) {
            glm::vec2 a = fill_[contour.fill_first + i] + offset;
            glm::vec2 b = fill_[contour.fill_first + (i + 1) % contour.fill_count] + offset;
            if (a.y == b.y) {
                continue;
            }
            std::int32_t dir = 1;
            if (a.y > b.y) {
                std::swap(a, b);
                dir = -1;
            }
            const int y_first = std::max(0, static_cast<int>(std::ceil(a.y - 0.5f)));
            const int y_end = std::min(height, static_cast<int>(std::ceil(b.y - 0.5f)));
            const float dxdy = (b.x - a.x) / (b.y - a.y);
            for (int y = y_first; y < y_end; ++y) {
                const float x = a.x + (static_cast<float>(y) + 0.5f - a.y) * dxdy;
                const int column = std::clamp(static_cast<int>(std::ceil(x - 0.5f)), 0, width);
                winding_[static_cast<std::size_t>(y) * row_cells + static_cast<std::size_t>(column)] += dir;
            }
        }
    }
    for (int y = 0; y < height; ++y) {
        std::int32_t* row = winding_.data() + static_cast<std::size_t>(y) * row_cells;
        std::int32_t sum = 0;
        for (int x = 0; x < width; ++x) {
            sum += row[x];
            row[x] = sum;
        }
    }
    // A pixel the fill covers. A convex fill is a fan with back faces culled: only a front-facing (positive) winding.
    const auto filled = [&](int x, int y) {
        const std::int32_t w = winding_[static_cast<std::size_t>(y) * row_cells + static_cast<std::size_t>(x)];
        return convex_ ? w > 0 : w != 0;
    };

    // The fringe strips: GL triangle strips with back faces culled (front is counterclockwise on screen, a negative
    // cross product with y down), sampled at pixel centers with a top-left rule, blended source over.
    for (const Contour& contour : contours_) {
        for (std::uint32_t i = 0; i + 2 < contour.fringe_count; ++i) {
            const FringeVertex* v = fringe_.data() + contour.fringe_first + i;
            FringeVertex t[3] = {v[0], v[1], v[2]};
            if ((i & 1u) != 0) {
                std::swap(t[0], t[1]);
            }
            for (FringeVertex& vertex : t) {
                vertex.p += offset;
            }
            const float area = cross(t[1].p - t[0].p, t[2].p - t[0].p);
            if (!(area < 0.0f)) {
                continue;
            }
            const glm::vec2 lo = glm::min(t[0].p, glm::min(t[1].p, t[2].p));
            const glm::vec2 hi = glm::max(t[0].p, glm::max(t[1].p, t[2].p));
            const int x0 = std::max(0, static_cast<int>(std::ceil(lo.x - 0.5f)));
            const int x1 = std::min(width - 1, static_cast<int>(std::floor(hi.x - 0.5f)));
            const int y0 = std::max(0, static_cast<int>(std::ceil(lo.y - 0.5f)));
            const int y1 = std::min(height - 1, static_cast<int>(std::floor(hi.y - 0.5f)));
            if (x0 > x1 || y0 > y1) {
                continue;
            }
            // Edge k runs from t[k] to t[k + 1]; its function is >= 0 inside. Left edges (going down) and top edges
            // (going left) own the centers they pass through.
            glm::vec2 from[3];
            glm::vec2 dir[3];
            bool owns[3];
            for (int k = 0; k < 3; ++k) {
                from[k] = t[k].p;
                dir[k] = t[(k + 1) % 3].p - t[k].p;
                owns[k] = dir[k].y > 0.0f || (dir[k].y == 0.0f && dir[k].x < 0.0f);
            }
            const float inverse_area = 1.0f / -area;
            for (int y = y0; y <= y1; ++y) {
                float* row = coverage_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
                for (int x = x0; x <= x1; ++x) {
                    const glm::vec2 c{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
                    float e[3];
                    bool inside = true;
                    for (int k = 0; k < 3 && inside; ++k) {
                        e[k] = -cross(dir[k], c - from[k]);
                        inside = e[k] > 0.0f || (e[k] == 0.0f && owns[k]);
                    }
                    if (!inside || (!convex_ && filled(x, y))) {
                        continue;
                    }
                    // e[1] is the edge opposite t[0], e[2] opposite t[1], e[0] opposite t[2].
                    const float u = (e[1] * t[0].u + e[2] * t[1].u + e[0] * t[2].u) * inverse_area;
                    const float alpha = std::clamp(1.0f - std::abs(2.0f * u - 1.0f), 0.0f, 1.0f);
                    row[x] = alpha + row[x] * (1.0f - alpha);
                }
            }
        }
    }

    for (int y = 0; y < height; ++y) {
        const float* row = coverage_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        std::uint8_t* dst = out + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        for (int x = 0; x < width; ++x) {
            const float coverage = filled(x, y) ? 1.0f : row[x];
            dst[x] = static_cast<std::uint8_t>(std::clamp(coverage, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

}
