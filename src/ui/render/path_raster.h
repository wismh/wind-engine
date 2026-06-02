#pragma once

#include "ui/painter.h"
#include "ui/render/affine.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace engine::ui {

// fill_path and stroke_path on the CPU. Curves flatten until they sit within `tolerance` of the polyline, and points
// closer than 0.01 px merge. A fill forces every contour to the winding of the path's largest one (shoelace of the
// points and control points): that winding is solid, the opposite is a hole. The fill is an inset of half a pixel plus
// a fringe from 1.5 px inside to 0.5 px outside, miter joins, limit 2.4. A pixel whose center has a nonzero winding of
// the inset is covered; anywhere else the front-facing fringe triangles blend source-over with alpha 1 - |2u - 1|. One
// convex contour is the inset fan with back faces culled and a half fringe (0.5 px inside). An edge ramps over one
// pixel centered on it, and a stem thinner than a pixel draws about a pixel wide.
//
// A stroke is the union of capsules of radius `width / 2` along the flattened segments: a round cap on an open end, a
// round join where segments meet. `Close` joins the last point to the first; without it the ends stay apart. Overlaps
// take the maximum coverage. The same one-pixel ramp sits on the capsule edge. Scratch buffers stay between calls, so
// a warm rasterizer does not allocate for a path no bigger than one it saw.
class PathRasterizer {
public:
    // Flattens `path` mapped by `to_raster` (into raster pixels, y down) and builds the fill and fringe geometry.
    // `tolerance` is the curve tolerance in raster units (0.25 at pixel ratio 1). Returns false when nothing is left
    // to fill. A `Close` segment changes nothing: a fill contour always closes.
    bool flatten(std::span<const PathSegment> path, const Affine2& to_raster, float tolerance = 0.25f);

    // Flattens `path` the same way and builds a stroke of `width` raster pixels (full width, round cap, round join).
    // Returns false when nothing is left to draw.
    bool flatten_stroke(std::span<const PathSegment> path, const Affine2& to_raster, float width,
            float tolerance = 0.25f);

    // Bounds of the fill or stroke geometry, raster pixels: every pixel the path can cover has its center inside.
    [[nodiscard]] glm::vec2 bounds_min() const noexcept { return min_; }
    [[nodiscard]] glm::vec2 bounds_max() const noexcept { return max_; }

    // Coverage of the flattened path shifted by `offset`, into `width` x `height` bytes at `out` (rows `stride` bytes
    // apart). Pixel (x, y) is sampled at (x + 0.5, y + 0.5). Every byte of the rectangle is written.
    void rasterize(glm::vec2 offset, int width, int height, std::uint8_t* out, int stride);

private:
    struct Point {
        glm::vec2 p{};
        // Unit direction to the next point and the distance to it.
        glm::vec2 d{};
        float len = 0.0f;
        // The miter extrusion, pointing into the fill.
        glm::vec2 dm{};
        std::uint8_t flags = 0;
    };
    struct Contour {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
        float control_area = 0.0f;
        bool closed = false;
        bool convex = false;
        std::uint32_t fill_first = 0;
        std::uint32_t fill_count = 0;
        std::uint32_t fringe_first = 0;
        std::uint32_t fringe_count = 0;
    };
    struct FringeVertex {
        glm::vec2 p{};
        float u = 0.0f;
    };

    struct StrokeSegment {
        glm::vec2 a{};
        glm::vec2 b{};
    };

    void add_point(glm::vec2 p, std::uint8_t flags);
    void tessellate(glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, glm::vec2 p4, int level, std::uint8_t flags);
    void calculate_joins();
    void expand_fill();
    void bevel_join(const Point& p0, const Point& p1, float lw, float rw, float lu, float ru);
    void fringe(glm::vec2 p, float u) { fringe_.push_back({p, u}); }
    void rasterize_stroke(glm::vec2 offset, int width, int height, std::uint8_t* out, int stride);

    float tolerance_ = 0.25f;
    float half_width_ = 0.0f;
    bool convex_ = false;
    bool stroking_ = false;
    std::vector<Point> points_;
    std::vector<Contour> contours_;
    std::vector<glm::vec2> fill_;
    std::vector<FringeVertex> fringe_;
    std::vector<StrokeSegment> strokes_;
    std::vector<std::int32_t> winding_;
    std::vector<float> coverage_;
    glm::vec2 min_{};
    glm::vec2 max_{};
};

}
