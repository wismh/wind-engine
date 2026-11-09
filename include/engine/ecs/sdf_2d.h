#pragma once

// docs/tech/modules/ECS.md (SDF fields)

#include <engine/ecs/grid_query.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine {

// A 2D signed distance field for destructible terrain: negative inside solid, positive in empty space, zero on the
// surface. Pure math like the grid queries: no world, no components, nothing registered.
//
// Grid: samples sit at cell corners. Sample (i, j) is at `origin + cell_size * (i, j)`; cell (i, j) spans samples
// (i, j) to (i + 1, j + 1). +Y is up, as in the grid queries. Chunk (cx, cy) owns the 32 x 32 samples and the 32 x 32
// cells whose indices lie in [32 cx, 32 cx + 32) x [32 cy, 32 cy + 32); chunk coordinates may be negative. Every sample
// belongs to exactly one chunk, so a seam holds no duplicate value. The cells on a chunk's right and top rows read
// samples of the neighbouring chunks. With `origin` at a tilemap's top-left corner and `cell_size` its (square) cell
// size, the field's chunk (cx, cy) covers the same cells as tilemap chunk (cx, -cy - 1), since tilemap rows run down.
//
// Storage: one int16 per sample, a distance quantized over [-band, +band] (`band / 32767` world units per step) and
// clamped there. Chunks are allocated only when an edit changes them; everywhere else reads `outside`.

// Cells (and samples) per chunk side.
inline constexpr int kSdfChunkCells = 32;
inline constexpr int kSdfChunkSamples = kSdfChunkCells * kSdfChunkCells;
// The quantized value of +band.
inline constexpr std::int16_t kSdfQuantMax = 32767;

// What the field reads where no chunk is allocated.
enum class SdfOutside {
    // +band: build terrain into empty space.
    Empty,
    // -band: dig into a solid world.
    Solid,
};

struct SdfField2DDesc {
    // World units per cell. Non-finite or not positive: 1.
    float cell_size = 1.0f;
    // The largest representable |distance|, in world units. Non-finite or not positive: 4 cells. Below one cell: one
    // cell. A wider band costs more per edit and per `renormalize`, and gives a finer quantization step only relative
    // to itself.
    float band = 4.0f;
    // World position of sample (0, 0). Non-finite: zero.
    glm::vec2 origin{0.0f};
    SdfOutside outside = SdfOutside::Empty;
};

struct SdfHit2D {
    glm::vec2 point{0.0f};
    // Unit surface normal at `point` (`gradient`), pointing out of the solid. Zero where the field is flat.
    glm::vec2 normal{0.0f};
    // World units along the normalized direction. 0 when the ray starts inside.
    float distance = 0.0f;
};

// An axis-aligned rectangle in the field's frame.
struct SdfBounds2D {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
};

struct SdfSweep2D {
    // Of `delta`, in [0, 1]: where the circle first touches. 0 when it starts touching or penetrating.
    float fraction = 0.0f;
    // The circle center at `fraction`.
    glm::vec2 center{0.0f};
    // The contact point on the surface.
    glm::vec2 point{0.0f};
    // Unit surface normal at the center, pointing out of the solid. Zero where the field is flat.
    glm::vec2 normal{0.0f};
    // How far the circle starts inside the surface (`radius - distance`); 0 for a contact found while moving.
    float depth = 0.0f;
};

class SdfField2D {
public:
    SdfField2D();
    explicit SdfField2D(const SdfField2DDesc& desc);

    [[nodiscard]] float cell_size() const {
        return cell_size_;
    }
    [[nodiscard]] float band() const {
        return band_;
    }
    [[nodiscard]] glm::vec2 origin() const {
        return origin_;
    }
    [[nodiscard]] SdfOutside outside() const {
        return outside_;
    }

    // `band` to `kSdfQuantMax`, rounded half up, clamped; non-finite reads as the clamp of its sign (NaN: +band).
    [[nodiscard]] std::int16_t quantize(float distance) const;
    [[nodiscard]] float dequantize(std::int16_t value) const;

    // The chunk that owns sample or cell `index`.
    [[nodiscard]] static glm::ivec2 chunk_of(glm::ivec2 index);

    // --- Queries: const, deterministic, no allocation. Non-finite or very far (2^24 cells) positions read `outside`.

    // The stored distance at sample `index`.
    [[nodiscard]] float sample_at(glm::ivec2 index) const;
    // Bilinear distance at world position `p`.
    [[nodiscard]] float sample(glm::vec2 p) const;
    // `sample` at each of `points` into `out` (the first `min(points.size(), out.size())`), bit-identical to calling
    // it per point, but a run of points in one chunk finds the chunk once: for many nearby points (a collider's
    // outline).
    void sample_many(std::span<const glm::vec2> points, std::span<float> out) const;
    // Unit normal at `p`: the analytic gradient of the bilinear cell, or central differences half a cell to each side
    // when that is degenerate (a saddle or a flat cell); zero when both are flat.
    [[nodiscard]] glm::vec2 gradient(glm::vec2 p) const;

    // Sphere tracing from `origin` along `direction` (normalized here) up to `max_distance` world units. A step is the
    // sampled distance, at least 1/10 of a cell, so a feature thinner than that can be stepped over. A sign change is
    // refined by bisection to within 1e-4 of a cell of the zero level; the hit point is on the empty side. A ray that
    // starts at or inside the surface hits at distance 0. A ray that only grazes the surface (never reaches d <= 0)
    // misses. With `SdfOutside::Empty` the ray is clipped to the allocated chunks first, so a long ray past the
    // terrain costs nothing. nullopt: a miss, a zero or non-finite direction, a negative or non-finite
    // `max_distance`, a non-finite origin.
    [[nodiscard]] std::optional<SdfHit2D> raymarch(glm::vec2 origin, glm::vec2 direction, float max_distance) const;

    // First contact of a circle moving by `delta` with the surface: conservative advancement on `sample - radius`
    // with the stepping and refinement of `raymarch`. A circle that starts touching or penetrating reports fraction
    // 0 and its `depth`, even with a zero delta. `radius` must be below `band`: the field cannot tell a farther
    // surface from a nearer one beyond it. nullopt: no contact along `delta` (or a zero delta while not touching), a
    // negative radius, a radius of `band` or more, a non-finite input.
    [[nodiscard]] std::optional<SdfSweep2D> sweep_circle(glm::vec2 center, float radius, glm::vec2 delta) const;

    // --- Edits. Each returns the chunks it dirtied, sorted by y then x, unique, and bumps each one's revision once.
    // A chunk is dirty when any cell it owns changed, that is, when any of the cell's four corner samples changed
    // value; a changed sample in a chunk's first column or row therefore also dirties the chunk to its left or
    // below. A dirty chunk is allocated (filled with `outside`) if it was not. An edit that changes no sample
    // returns an empty list and bumps nothing.
    //
    // CSG with the exact shape SDF, over the samples within the shape's bounds grown by `band` (beyond them the shape
    // cannot change a clamped value): fill = min(d, shape), carve = max(d, -shape), in the quantized domain. The
    // result is a distance bound, not a distance, where shapes overlap; `renormalize` restores distances.
    // A radius that is not positive, or a non-finite input, is a no-op.
    std::vector<glm::ivec2> carve_circle(glm::vec2 center, float radius);
    std::vector<glm::ivec2> fill_circle(glm::vec2 center, float radius);
    // `vertices` is a convex polygon in world space, counter-clockwise. Fewer than 3 vertices, a non-finite vertex, a
    // zero-length edge, a clockwise (right) turn, or a non-positive area is a no-op (the rules of
    // `PolygonCollider2D`); a collinear turn is kept. There is no vertex cap.
    std::vector<glm::ivec2> carve_polygon(std::span<const glm::vec2> vertices);
    std::vector<glm::ivec2> fill_polygon(std::span<const glm::vec2> vertices);

    // Builds the field from a solid-cell mask over mask cells [min_cell, min_cell + size). Mask cell (x, y) is the
    // square of field cells [x, x + 1) * `cells_per_mask_cell` on each axis (+Y up, like the grid queries; a tile
    // layer is `map.tile(layer, c.x, -c.y - 1) != 0`). Samples get the exact distance to the union of solid squares,
    // clamped to the band; a sample on its boundary is 0. Mask cells outside the rectangle read as `outside`.
    // Samples inside the rectangle (corners included) are overwritten; samples in the band-wide margin around it are
    // merged, min with `Empty` and max with `Solid`, so the mask joins terrain already there. A shape one field cell
    // thick has no negative sample: pass `cells_per_mask_cell = 2` or more for one-tile platforms. Cost: one
    // predicate call per mask cell, and per written sample a scan of the mask cells within `band`. A non-positive
    // size or `cells_per_mask_cell` is a no-op.
    std::vector<glm::ivec2> fill_from_mask(
            glm::ivec2 min_cell, glm::ivec2 size, CellPredicate<glm::ivec2> is_solid, int cells_per_mask_cell = 1);

    // Recomputes the chunk's samples as distances to the zero level, keeping each sample's sign (negative stays
    // negative, zero and positive stay non-negative). The interface is the piecewise-linear zero crossing of the
    // samples (marching squares, crossings by linear interpolation along cell edges, a saddle split by the cell's
    // mean) read from the chunk plus a halo of `ceil(band / cell_size) + 1` samples from its neighbours; each sample
    // takes the exact distance to the nearest segment within the band, or the band. Brute force within the band:
    // per sample, the segments of the (2 h) ^ 2 cells around it (h the halo), so about 1024 * (2 h) ^ 2 cell visits
    // per chunk (about 100k for a band of 4 cells).
    // A sample's result depends only on the samples within the halo of it, so results match across seams. An
    // unallocated chunk is a no-op.
    std::vector<glm::ivec2> renormalize(glm::ivec2 chunk);
    // Renormalizes every listed chunk from the field as it was before any of them changed, so the result does not
    // depend on the list's order and equals renormalizing them all at once. Duplicates are ignored.
    std::vector<glm::ivec2> renormalize(std::span<const glm::ivec2> chunks);

    // Overwrites the chunk's samples with `samples` (the layout of `chunk_samples`), for loading a saved field. The
    // chunk is allocated if it was not; the returned dirty list follows the edit rules above, and a newly allocated
    // chunk is always in it. A span that is not `kSdfChunkSamples` long is a no-op.
    std::vector<glm::ivec2> assign_chunk(glm::ivec2 chunk, std::span<const std::int16_t> samples);

    // --- Chunks.
    [[nodiscard]] bool has_chunk(glm::ivec2 chunk) const;
    [[nodiscard]] std::size_t chunk_count() const {
        return chunks_.size();
    }
    // Where the field can read anything but `outside`: the rectangle of allocated chunks grown by one cell on each
    // side (the cells next to it still read its samples). Beyond it every query reads `outside`. nullopt when no chunk
    // is allocated.
    [[nodiscard]] std::optional<SdfBounds2D> allocated_bounds() const;
    // Allocated chunks, sorted by y then x.
    [[nodiscard]] std::vector<glm::ivec2> chunk_coords() const;
    // Bumped once by every edit that dirties the chunk; 0 for an unallocated chunk. A chunk allocated by an edit
    // starts at 1.
    [[nodiscard]] std::uint64_t revision(glm::ivec2 chunk) const;
    // The chunk's quantized samples, row-major from the bottom row: local (x, y) is `y * kSdfChunkCells + x`. Empty
    // for an unallocated chunk.
    [[nodiscard]] std::span<const std::int16_t> chunk_samples(glm::ivec2 chunk) const;
    // Bumped once by every edit that dirties at least one chunk; 0 for a field never edited. A cache of the field (the
    // 2D solver's SdfCollider2D slots) compares it to find an edit it was not told about.
    [[nodiscard]] std::uint64_t edit_count() const {
        return edit_count_;
    }
    // FNV-1a 64 of what the field answers: cell size, band, origin, `outside`, then every allocated chunk in row-major
    // order (`chunk_coords`) with its samples. Two fields with the same hash sample the same everywhere; revisions and
    // `edit_count` (bookkeeping) are not in it.
    [[nodiscard]] std::uint64_t hash() const;

private:
    struct Chunk {
        std::array<std::int16_t, kSdfChunkSamples> samples{};
        std::uint64_t revision = 0;
    };
    class Cursor;
    class DirtyTracker;

    [[nodiscard]] const Chunk* find(glm::ivec2 chunk) const;
    [[nodiscard]] Chunk* find(glm::ivec2 chunk);
    Chunk& allocate(glm::ivec2 chunk);
    template <class F>
    std::vector<glm::ivec2> apply(glm::ivec2 lo, glm::ivec2 hi, const F& value);
    std::vector<glm::ivec2> finish(DirtyTracker& dirty);
    [[nodiscard]] std::optional<float> march(glm::vec2 origin, glm::vec2 direction, float max_distance,
            float offset) const;

    float cell_size_ = 1.0f;
    float band_ = 4.0f;
    glm::vec2 origin_{0.0f};
    SdfOutside outside_ = SdfOutside::Empty;
    std::int16_t outside_value_ = kSdfQuantMax;
    // `kSdfQuantMax / band_` and `band_ / kSdfQuantMax`.
    float to_quant_ = 1.0f;
    float from_quant_ = 1.0f;
    std::unordered_map<std::uint64_t, Chunk> chunks_;
    // The rectangle of allocated chunks; valid when `chunks_` is not empty.
    glm::ivec2 chunk_min_{0};
    glm::ivec2 chunk_max_{0};
    std::uint64_t edit_count_ = 0;
};

}
