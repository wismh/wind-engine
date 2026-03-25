#pragma once

#include "ui/painter.h"
#include "ui/render/affine.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace engine::ui {

// fill_path on the CPU, coverage for coverage what nvgFill with NVG_ANTIALIAS draws. The path is flattened as NanoVG
// flattens it (nvg__tesselateBezier, points closer than 0.01 px merged), every contour's direction forced first the way
// the NanoVG painter forced it through nvgPathWinding: a contour wound like the path's largest one (by the shoelace
// area of its points and control points) is solid, any other is a hole. Then nvg__expandFill's geometry is built
// (fringe 1 px, miter joins, limit 2.4): each contour inset by half a pixel, and a fringe strip from 1.5 px inside (u =
// 0) to 0.5 px outside (u = 1). Coverage is sampled at pixel centers like the GPU does: a pixel whose center has a
// nonzero winding of the inset contours is 1 (the stencil pass), any other gets the front-facing fringe triangles over
// it, each blended source-over with alpha 1 - |2u - 1|. A single convex contour is drawn as NanoVG's convex fill: the
// inset fan with back faces culled, then a half fringe (0.5 px inside, u = 0.5) over everything. So an edge ramps
// linearly over one pixel centered on it, and a stem thinner than a pixel is drawn about a pixel wide, as bold as
// NanoVG draws it. Scratch buffers are kept between calls, so a warm rasterizer does not allocate for a path no bigger
// than one it saw.
class PathRasterizer {
public:
    // Flattens `path` mapped by `to_raster` (into raster pixels, y down) and builds the fill and fringe geometry.
    // `tolerance` is NanoVG's tessTol in raster units (0.25 at pixel ratio 1). Returns false when nothing is left to
    // fill.
    bool flatten(std::span<const PathSegment> path, const Affine2& to_raster, float tolerance = 0.25f);

    // Bounds of the fill and fringe geometry, raster pixels: every pixel the path can cover has its center inside.
    [[nodiscard]] glm::vec2 bounds_min() const noexcept { return min_; }
    [[nodiscard]] glm::vec2 bounds_max() const noexcept { return max_; }

    // Coverage of the flattened path shifted by `offset`, into `width` x `height` bytes at `out` (rows `stride` bytes
    // apart). Pixel (x, y) is sampled at (x + 0.5, y + 0.5). Every byte of the rectangle is written.
    void rasterize(glm::vec2 offset, int width, int height, std::uint8_t* out, int stride);

private:
    struct Point {
        glm::vec2 p{};
        // Unit direction to the next point and the distance to it (NVGpoint dx, dy, len).
        glm::vec2 d{};
        float len = 0.0f;
        // The miter extrusion, pointing into the fill (NVGpoint dmx, dmy).
        glm::vec2 dm{};
        std::uint8_t flags = 0;
    };
    struct Contour {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
        float control_area = 0.0f;
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

    void add_point(glm::vec2 p, std::uint8_t flags);
    void tessellate(glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, glm::vec2 p4, int level, std::uint8_t flags);
    void calculate_joins();
    void expand_fill();
    void bevel_join(const Point& p0, const Point& p1, float lw, float rw, float lu, float ru);
    void fringe(glm::vec2 p, float u) { fringe_.push_back({p, u}); }

    float tolerance_ = 0.25f;
    bool convex_ = false;
    std::vector<Point> points_;
    std::vector<Contour> contours_;
    std::vector<glm::vec2> fill_;
    std::vector<FringeVertex> fringe_;
    std::vector<std::int32_t> winding_;
    std::vector<float> coverage_;
    glm::vec2 min_{};
    glm::vec2 max_{};
};

}
