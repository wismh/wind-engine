#include <engine/ecs/sdf_2d.h>

#include "core/state_hash.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace engine {
namespace {

// Grid coordinates from here on read `outside`: a float no longer tells one cell from the next. 2^24.
constexpr float kMaxGrid = 16777216.0f;
constexpr long long kMaxIndex = 16777216;
// Raymarch and sweep: the smallest step, and the bisection stop, in cells.
constexpr float kMinStepCells = 0.1f;
constexpr float kRefineCells = 1.0e-4f;
constexpr int kRefineIterations = 40;
// A bilinear gradient shorter than this (in world units per cell, relative to the cell) is degenerate.
constexpr float kDegenerateGradient = 1.0e-3f;
constexpr float kFlatGradient = 1.0e-6f;

constexpr int kShift = 5;
constexpr int kLocalMask = kSdfChunkCells - 1;
static_assert((1 << kShift) == kSdfChunkCells);

std::uint64_t chunk_key(glm::ivec2 chunk) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunk.x)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunk.y));
}

int local_index(glm::ivec2 sample) {
    return (sample.y & kLocalMask) * kSdfChunkCells + (sample.x & kLocalMask);
}

bool finite(glm::vec2 v) {
    return std::isfinite(v.x) && std::isfinite(v.y);
}

bool in_grid(glm::vec2 g) {
    return finite(g) && std::abs(g.x) < kMaxGrid && std::abs(g.y) < kMaxGrid;
}

// `a / b` rounded down, for `b > 0`.
int floor_div(int a, int b) {
    const int q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

bool row_major_less(glm::ivec2 a, glm::ivec2 b) {
    return a.y != b.y ? a.y < b.y : a.x < b.x;
}

float cross(glm::vec2 a, glm::vec2 b) {
    return a.x * b.y - a.y * b.x;
}

bool valid_polygon(std::span<const glm::vec2> v) {
    const std::size_t n = v.size();
    if (n < 3) {
        return false;
    }
    float area = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 a = v[i];
        const glm::vec2 b = v[(i + 1) % n];
        const glm::vec2 c = v[(i + 2) % n];
        if (!finite(a)) {
            return false;
        }
        const glm::vec2 e = b - a;
        if (e.x == 0.0f && e.y == 0.0f) {
            return false;
        }
        if (cross(e, c - b) < 0.0f) {
            return false;
        }
        area += cross(a, b);
    }
    return area > 0.0f;
}

// Exact signed distance to a valid convex CCW polygon.
float polygon_distance(std::span<const glm::vec2> v, glm::vec2 p) {
    const std::size_t n = v.size();
    float best = std::numeric_limits<float>::infinity();
    bool inside = true;
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 a = v[i];
        const glm::vec2 e = v[(i + 1) % n] - a;
        const glm::vec2 w = p - a;
        const float h = std::clamp(glm::dot(w, e) / glm::dot(e, e), 0.0f, 1.0f);
        const glm::vec2 diff = w - e * h;
        best = std::min(best, glm::dot(diff, diff));
        if (cross(e, w) < 0.0f) {
            inside = false;
        }
    }
    const float d = std::sqrt(best);
    return inside ? -d : d;
}

// Squared distance from the origin to segment [a, b].
float segment_distance2(glm::vec2 a, glm::vec2 b) {
    const glm::vec2 e = b - a;
    const float len2 = glm::dot(e, e);
    const float h = len2 > 0.0f ? std::clamp(-glm::dot(a, e) / len2, 0.0f, 1.0f) : 0.0f;
    const glm::vec2 q = a + e * h;
    return glm::dot(q, q);
}

struct Segment {
    glm::vec2 a{0.0f};
    glm::vec2 b{0.0f};
};

// The zero crossings of one cell as segments in cell-local [0, 1] coordinates (marching squares). Inside is `v < 0`.
// Corners: v00 (0, 0), v10 (1, 0), v11 (1, 1), v01 (0, 1). Returns the segment count (0, 1 or 2).
int cell_segments(float v00, float v10, float v11, float v01, Segment out[2]) {
    const bool i00 = v00 < 0.0f;
    const bool i10 = v10 < 0.0f;
    const bool i11 = v11 < 0.0f;
    const bool i01 = v01 < 0.0f;
    // Edges: 0 bottom (00-10), 1 right (10-11), 2 top (01-11), 3 left (00-01).
    glm::vec2 points[4];
    bool cut[4] = {i00 != i10, i10 != i11, i01 != i11, i00 != i01};
    if (cut[0]) {
        points[0] = {v00 / (v00 - v10), 0.0f};
    }
    if (cut[1]) {
        points[1] = {1.0f, v10 / (v10 - v11)};
    }
    if (cut[2]) {
        points[2] = {v01 / (v01 - v11), 1.0f};
    }
    if (cut[3]) {
        points[3] = {0.0f, v00 / (v00 - v01)};
    }
    const int count = static_cast<int>(cut[0]) + cut[1] + cut[2] + cut[3];
    if (count == 2) {
        int found[2];
        int k = 0;
        for (int e = 0; e < 4; ++e) {
            if (cut[e]) {
                found[k++] = e;
            }
        }
        out[0] = {points[found[0]], points[found[1]]};
        return 1;
    }
    if (count == 4) {
        // A saddle: the mean says whether the diagonal of the inside corners is connected through the center.
        const bool center_inside = (v00 + v10 + v11 + v01) * 0.25f < 0.0f;
        // Pairing A cuts off corners 10 and 01; pairing B cuts off corners 00 and 11.
        const bool pairing_a = i00 == center_inside;
        if (pairing_a) {
            out[0] = {points[0], points[1]};
            out[1] = {points[2], points[3]};
        } else {
            out[0] = {points[3], points[0]};
            out[1] = {points[1], points[2]};
        }
        return 2;
    }
    return 0;
}

}

// Reads samples with a one-chunk cache; lives for one query, never stored.
class SdfField2D::Cursor {
public:
    explicit Cursor(const SdfField2D& field) : field_(field) {}

    std::int16_t raw(glm::ivec2 sample) {
        const glm::ivec2 chunk = chunk_of(sample);
        if (!valid_ || chunk != cached_) {
            cached_ = chunk;
            chunk_ = field_.find(chunk);
            valid_ = true;
        }
        return chunk_ != nullptr ? chunk_->samples[static_cast<std::size_t>(local_index(sample))]
                                 : field_.outside_value_;
    }

    float at(glm::ivec2 sample) {
        return field_.dequantize(raw(sample));
    }

    struct Patch {
        float d00 = 0.0f;
        float d10 = 0.0f;
        float d01 = 0.0f;
        float d11 = 0.0f;
        float fx = 0.0f;
        float fy = 0.0f;
    };

    // The bilinear cell under world position `p`; nullopt outside the usable grid.
    std::optional<Patch> patch(glm::vec2 p) {
        const glm::vec2 g = (p - field_.origin_) / field_.cell_size_;
        if (!in_grid(g)) {
            return std::nullopt;
        }
        const glm::vec2 base = glm::floor(g);
        const glm::ivec2 cell{static_cast<int>(base.x), static_cast<int>(base.y)};
        Patch out;
        out.d00 = at(cell);
        out.d10 = at({cell.x + 1, cell.y});
        out.d01 = at({cell.x, cell.y + 1});
        out.d11 = at({cell.x + 1, cell.y + 1});
        out.fx = g.x - base.x;
        out.fy = g.y - base.y;
        return out;
    }

    float sample(glm::vec2 p) {
        const std::optional<Patch> c = patch(p);
        if (!c) {
            return field_.dequantize(field_.outside_value_);
        }
        const float d0 = c->d00 + (c->d10 - c->d00) * c->fx;
        const float d1 = c->d01 + (c->d11 - c->d01) * c->fx;
        return d0 + (d1 - d0) * c->fy;
    }

    glm::vec2 gradient(glm::vec2 p) {
        const std::optional<Patch> c = patch(p);
        if (!c) {
            return glm::vec2{0.0f};
        }
        glm::vec2 g{(c->d10 - c->d00) * (1.0f - c->fy) + (c->d11 - c->d01) * c->fy,
                (c->d01 - c->d00) * (1.0f - c->fx) + (c->d11 - c->d10) * c->fx};
        const float cs = field_.cell_size_;
        if (glm::length(g) < kDegenerateGradient * cs) {
            const float h = 0.5f * cs;
            g = glm::vec2{sample(p + glm::vec2{h, 0.0f}) - sample(p - glm::vec2{h, 0.0f}),
                    sample(p + glm::vec2{0.0f, h}) - sample(p - glm::vec2{0.0f, h})};
            if (glm::length(g) < kFlatGradient * cs) {
                return glm::vec2{0.0f};
            }
        }
        return glm::normalize(g);
    }

private:
    const SdfField2D& field_;
    const Chunk* chunk_ = nullptr;
    glm::ivec2 cached_{0};
    bool valid_ = false;
};

// Collects the chunks one edit dirtied.
class SdfField2D::DirtyTracker {
public:
    // A chunk whose samples changed; a change in its first column, row, or at its first sample also changed cells of
    // the chunk to the left, below, or diagonally below-left.
    void add(glm::ivec2 chunk, bool column0, bool row0, bool corner) {
        chunks_.push_back(chunk);
        if (column0) {
            chunks_.push_back({chunk.x - 1, chunk.y});
        }
        if (row0) {
            chunks_.push_back({chunk.x, chunk.y - 1});
        }
        if (corner) {
            chunks_.push_back({chunk.x - 1, chunk.y - 1});
        }
    }

    std::vector<glm::ivec2> take() {
        std::sort(chunks_.begin(), chunks_.end(), row_major_less);
        chunks_.erase(std::unique(chunks_.begin(), chunks_.end()), chunks_.end());
        return std::move(chunks_);
    }

private:
    std::vector<glm::ivec2> chunks_;
};

SdfField2D::SdfField2D() : SdfField2D(SdfField2DDesc{}) {}

SdfField2D::SdfField2D(const SdfField2DDesc& desc) {
    cell_size_ = std::isfinite(desc.cell_size) && desc.cell_size > 0.0f ? desc.cell_size : 1.0f;
    band_ = std::isfinite(desc.band) && desc.band > 0.0f ? desc.band : 4.0f * cell_size_;
    band_ = std::max(band_, cell_size_);
    origin_ = finite(desc.origin) ? desc.origin : glm::vec2{0.0f};
    outside_ = desc.outside;
    outside_value_ = outside_ == SdfOutside::Solid ? static_cast<std::int16_t>(-kSdfQuantMax) : kSdfQuantMax;
    to_quant_ = static_cast<float>(kSdfQuantMax) / band_;
    from_quant_ = band_ / static_cast<float>(kSdfQuantMax);
}

std::int16_t SdfField2D::quantize(float distance) const {
    if (std::isnan(distance)) {
        return kSdfQuantMax;
    }
    const float q = distance * to_quant_;
    if (q >= static_cast<float>(kSdfQuantMax)) {
        return kSdfQuantMax;
    }
    if (q <= -static_cast<float>(kSdfQuantMax)) {
        return static_cast<std::int16_t>(-kSdfQuantMax);
    }
    return static_cast<std::int16_t>(std::floor(q + 0.5f));
}

float SdfField2D::dequantize(std::int16_t value) const {
    return static_cast<float>(value) * from_quant_;
}

glm::ivec2 SdfField2D::chunk_of(glm::ivec2 index) {
    return {index.x >> kShift, index.y >> kShift};
}

const SdfField2D::Chunk* SdfField2D::find(glm::ivec2 chunk) const {
    const auto it = chunks_.find(chunk_key(chunk));
    return it != chunks_.end() ? &it->second : nullptr;
}

SdfField2D::Chunk* SdfField2D::find(glm::ivec2 chunk) {
    const auto it = chunks_.find(chunk_key(chunk));
    return it != chunks_.end() ? &it->second : nullptr;
}

SdfField2D::Chunk& SdfField2D::allocate(glm::ivec2 chunk) {
    const auto [it, inserted] = chunks_.try_emplace(chunk_key(chunk));
    if (inserted) {
        it->second.samples.fill(outside_value_);
        if (chunks_.size() == 1) {
            chunk_min_ = chunk;
            chunk_max_ = chunk;
        } else {
            chunk_min_ = glm::min(chunk_min_, chunk);
            chunk_max_ = glm::max(chunk_max_, chunk);
        }
    }
    return it->second;
}

std::optional<SdfBounds2D> SdfField2D::allocated_bounds() const {
    if (chunks_.empty()) {
        return std::nullopt;
    }
    return SdfBounds2D{
            .min = origin_ + cell_size_ * (glm::vec2{chunk_min_ * kSdfChunkCells} - 1.0f),
            .max = origin_ + cell_size_ * (glm::vec2{(chunk_max_ + 1) * kSdfChunkCells} + 1.0f),
    };
}

bool SdfField2D::has_chunk(glm::ivec2 chunk) const {
    return find(chunk) != nullptr;
}

std::vector<glm::ivec2> SdfField2D::chunk_coords() const {
    std::vector<glm::ivec2> out;
    out.reserve(chunks_.size());
    for (const auto& [key, chunk] : chunks_) {
        out.push_back({static_cast<int>(static_cast<std::uint32_t>(key >> 32)),
                static_cast<int>(static_cast<std::uint32_t>(key & 0xffffffffu))});
    }
    std::sort(out.begin(), out.end(), row_major_less);
    return out;
}

std::uint64_t SdfField2D::hash() const {
    StateHasher hasher;
    hasher.f32(cell_size_);
    hasher.f32(band_);
    hasher.vec2(origin_);
    hasher.u8(static_cast<std::uint8_t>(outside_));
    const std::vector<glm::ivec2> coords = chunk_coords();
    hasher.u64(coords.size());
    for (const glm::ivec2 coord : coords) {
        hasher.i32(coord.x);
        hasher.i32(coord.y);
        for (const std::int16_t sample : find(coord)->samples) {
            hasher.u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(sample)));
            hasher.u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(sample) >> 8));
        }
    }
    return hasher.value();
}

std::uint64_t SdfField2D::revision(glm::ivec2 chunk) const {
    const Chunk* c = find(chunk);
    return c != nullptr ? c->revision : 0;
}

std::span<const std::int16_t> SdfField2D::chunk_samples(glm::ivec2 chunk) const {
    const Chunk* c = find(chunk);
    if (c == nullptr) {
        return {};
    }
    return {c->samples.data(), c->samples.size()};
}

float SdfField2D::sample_at(glm::ivec2 index) const {
    return Cursor{*this}.at(index);
}

float SdfField2D::sample(glm::vec2 p) const {
    return Cursor{*this}.sample(p);
}

void SdfField2D::sample_many(std::span<const glm::vec2> points, std::span<float> out) const {
    Cursor cursor{*this};
    const std::size_t count = std::min(points.size(), out.size());
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = cursor.sample(points[i]);
    }
}

glm::vec2 SdfField2D::gradient(glm::vec2 p) const {
    return Cursor{*this}.gradient(p);
}

std::optional<float> SdfField2D::march(glm::vec2 origin, glm::vec2 direction, float max_distance, float offset) const {
    float t = 0.0f;
    float t_end = max_distance;
    if (outside_ == SdfOutside::Empty) {
        // Beyond the allocated chunks (and the one cell next to them that still reads their samples) the field is
        // +band, so nothing is hit there: clip the ray to that rectangle.
        const std::optional<SdfBounds2D> bounds = allocated_bounds();
        if (!bounds) {
            return std::nullopt;
        }
        const glm::vec2 lo = bounds->min;
        const glm::vec2 hi = bounds->max;
        float t_near = 0.0f;
        float t_far = max_distance;
        for (int a = 0; a < 2; ++a) {
            if (direction[a] == 0.0f) {
                if (origin[a] < lo[a] || origin[a] > hi[a]) {
                    return std::nullopt;
                }
                continue;
            }
            float t0 = (lo[a] - origin[a]) / direction[a];
            float t1 = (hi[a] - origin[a]) / direction[a];
            if (t0 > t1) {
                std::swap(t0, t1);
            }
            t_near = std::max(t_near, t0);
            t_far = std::min(t_far, t1);
        }
        if (t_near > t_far) {
            return std::nullopt;
        }
        t = t_near;
        t_end = t_far;
    }

    Cursor cursor{*this};
    const float min_step = kMinStepCells * cell_size_;
    const float refine = kRefineCells * cell_size_;
    float f = cursor.sample(origin + direction * t) - offset;
    if (f <= 0.0f) {
        return t;
    }
    while (t < t_end) {
        float next = t + std::max(f, min_step);
        if (next > t_end) {
            next = t_end;
        }
        if (!(next > t)) {
            return std::nullopt;
        }
        const float f_next = cursor.sample(origin + direction * next) - offset;
        if (f_next <= 0.0f) {
            float lo = t;
            float hi = next;
            for (int i = 0; i < kRefineIterations && hi - lo > refine; ++i) {
                const float mid = 0.5f * (lo + hi);
                if (cursor.sample(origin + direction * mid) - offset <= 0.0f) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            return lo;
        }
        t = next;
        f = f_next;
    }
    return std::nullopt;
}

std::optional<SdfHit2D> SdfField2D::raymarch(glm::vec2 origin, glm::vec2 direction, float max_distance) const {
    if (!finite(origin) || !finite(direction) || !std::isfinite(max_distance) || max_distance < 0.0f) {
        return std::nullopt;
    }
    const float length = glm::length(direction);
    if (!(length > 0.0f) || !std::isfinite(length)) {
        return std::nullopt;
    }
    Cursor cursor{*this};
    if (cursor.sample(origin) <= 0.0f) {
        return SdfHit2D{origin, cursor.gradient(origin), 0.0f};
    }
    const glm::vec2 unit = direction / length;
    const std::optional<float> t = march(origin, unit, max_distance, 0.0f);
    if (!t) {
        return std::nullopt;
    }
    const glm::vec2 point = origin + unit * *t;
    return SdfHit2D{point, cursor.gradient(point), *t};
}

std::optional<SdfSweep2D> SdfField2D::sweep_circle(glm::vec2 center, float radius, glm::vec2 delta) const {
    if (!finite(center) || !finite(delta) || !std::isfinite(radius) || radius < 0.0f || radius >= band_) {
        return std::nullopt;
    }
    Cursor cursor{*this};
    const float d0 = cursor.sample(center);
    if (d0 - radius <= 0.0f) {
        const glm::vec2 normal = cursor.gradient(center);
        return SdfSweep2D{0.0f, center, center - normal * d0, normal, radius - d0};
    }
    const float length = glm::length(delta);
    if (!(length > 0.0f) || !std::isfinite(length)) {
        return std::nullopt;
    }
    const glm::vec2 unit = delta / length;
    const std::optional<float> t = march(center, unit, length, radius);
    if (!t) {
        return std::nullopt;
    }
    const glm::vec2 at = center + unit * *t;
    const glm::vec2 normal = cursor.gradient(at);
    return SdfSweep2D{std::clamp(*t / length, 0.0f, 1.0f), at, at - normal * radius, normal, 0.0f};
}

template <class F>
std::vector<glm::ivec2> SdfField2D::apply(glm::ivec2 lo, glm::ivec2 hi, const F& value) {
    DirtyTracker dirty;
    if (hi.x < lo.x || hi.y < lo.y) {
        return {};
    }
    const glm::ivec2 c_lo = chunk_of(lo);
    const glm::ivec2 c_hi = chunk_of(hi);
    for (int cy = c_lo.y; cy <= c_hi.y; ++cy) {
        for (int cx = c_lo.x; cx <= c_hi.x; ++cx) {
            const glm::ivec2 coord{cx, cy};
            const glm::ivec2 base = coord * kSdfChunkCells;
            const glm::ivec2 s_lo = glm::max(lo, base);
            const glm::ivec2 s_hi = glm::min(hi, base + (kSdfChunkCells - 1));
            Chunk* chunk = find(coord);
            if (chunk == nullptr) {
                // Allocate only when the edit changes something here.
                bool any = false;
                for (int y = s_lo.y; y <= s_hi.y && !any; ++y) {
                    for (int x = s_lo.x; x <= s_hi.x; ++x) {
                        if (value(glm::ivec2{x, y}, outside_value_) != outside_value_) {
                            any = true;
                            break;
                        }
                    }
                }
                if (!any) {
                    continue;
                }
                chunk = &allocate(coord);
            }
            bool changed = false;
            bool column0 = false;
            bool row0 = false;
            bool corner = false;
            for (int y = s_lo.y; y <= s_hi.y; ++y) {
                for (int x = s_lo.x; x <= s_hi.x; ++x) {
                    const glm::ivec2 s{x, y};
                    std::int16_t& slot = chunk->samples[static_cast<std::size_t>(local_index(s))];
                    const std::int16_t next = value(s, slot);
                    if (next == slot) {
                        continue;
                    }
                    slot = next;
                    changed = true;
                    column0 = column0 || x == base.x;
                    row0 = row0 || y == base.y;
                    corner = corner || (x == base.x && y == base.y);
                }
            }
            if (changed) {
                dirty.add(coord, column0, row0, corner);
            }
        }
    }
    return finish(dirty);
}

std::vector<glm::ivec2> SdfField2D::finish(DirtyTracker& dirty) {
    std::vector<glm::ivec2> out = dirty.take();
    for (const glm::ivec2 c : out) {
        ++allocate(c).revision;
    }
    if (!out.empty()) {
        ++edit_count_;
    }
    return out;
}

std::vector<glm::ivec2> SdfField2D::assign_chunk(glm::ivec2 chunk, std::span<const std::int16_t> samples) {
    if (samples.size() != static_cast<std::size_t>(kSdfChunkSamples)) {
        return {};
    }
    const bool allocated = has_chunk(chunk);
    Chunk& target = allocate(chunk);
    bool changed = !allocated;
    bool column0 = false;
    bool row0 = false;
    bool corner = false;
    for (int k = 0; k < kSdfChunkSamples; ++k) {
        const std::int16_t next = samples[static_cast<std::size_t>(k)];
        std::int16_t& slot = target.samples[static_cast<std::size_t>(k)];
        if (next == slot) {
            continue;
        }
        slot = next;
        changed = true;
        const int lx = k % kSdfChunkCells;
        const int ly = k / kSdfChunkCells;
        column0 = column0 || lx == 0;
        row0 = row0 || ly == 0;
        corner = corner || (lx == 0 && ly == 0);
    }
    DirtyTracker dirty;
    if (changed) {
        dirty.add(chunk, column0, row0, corner);
    }
    return finish(dirty);
}

namespace {

// The sample index range covering world box [lo, hi] grown by `grow`; false when it leaves the usable grid.
bool sample_range(glm::vec2 lo, glm::vec2 hi, float grow, glm::vec2 origin, float cell_size, glm::ivec2& out_lo,
        glm::ivec2& out_hi) {
    const glm::vec2 g_lo = glm::ceil((lo - grow - origin) / cell_size);
    const glm::vec2 g_hi = glm::floor((hi + grow - origin) / cell_size);
    if (!in_grid(g_lo) || !in_grid(g_hi)) {
        return false;
    }
    out_lo = {static_cast<int>(g_lo.x), static_cast<int>(g_lo.y)};
    out_hi = {static_cast<int>(g_hi.x), static_cast<int>(g_hi.y)};
    return true;
}

}

std::vector<glm::ivec2> SdfField2D::carve_circle(glm::vec2 center, float radius) {
    glm::ivec2 lo;
    glm::ivec2 hi;
    if (!finite(center) || !std::isfinite(radius) || !(radius > 0.0f) ||
            !sample_range(center - radius, center + radius, band_, origin_, cell_size_, lo, hi)) {
        return {};
    }
    return apply(lo, hi, [&](glm::ivec2 s, std::int16_t old) {
        const glm::vec2 p = origin_ + cell_size_ * glm::vec2{s};
        return std::max(old, quantize(radius - glm::length(p - center)));
    });
}

std::vector<glm::ivec2> SdfField2D::fill_circle(glm::vec2 center, float radius) {
    glm::ivec2 lo;
    glm::ivec2 hi;
    if (!finite(center) || !std::isfinite(radius) || !(radius > 0.0f) ||
            !sample_range(center - radius, center + radius, band_, origin_, cell_size_, lo, hi)) {
        return {};
    }
    return apply(lo, hi, [&](glm::ivec2 s, std::int16_t old) {
        const glm::vec2 p = origin_ + cell_size_ * glm::vec2{s};
        return std::min(old, quantize(glm::length(p - center) - radius));
    });
}

namespace {

void polygon_bounds(std::span<const glm::vec2> v, glm::vec2& lo, glm::vec2& hi) {
    lo = v[0];
    hi = v[0];
    for (const glm::vec2 p : v) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
}

}

std::vector<glm::ivec2> SdfField2D::carve_polygon(std::span<const glm::vec2> vertices) {
    if (!valid_polygon(vertices)) {
        return {};
    }
    glm::vec2 b_lo;
    glm::vec2 b_hi;
    polygon_bounds(vertices, b_lo, b_hi);
    glm::ivec2 lo;
    glm::ivec2 hi;
    if (!sample_range(b_lo, b_hi, band_, origin_, cell_size_, lo, hi)) {
        return {};
    }
    return apply(lo, hi, [&](glm::ivec2 s, std::int16_t old) {
        const glm::vec2 p = origin_ + cell_size_ * glm::vec2{s};
        return std::max(old, quantize(-polygon_distance(vertices, p)));
    });
}

std::vector<glm::ivec2> SdfField2D::fill_polygon(std::span<const glm::vec2> vertices) {
    if (!valid_polygon(vertices)) {
        return {};
    }
    glm::vec2 b_lo;
    glm::vec2 b_hi;
    polygon_bounds(vertices, b_lo, b_hi);
    glm::ivec2 lo;
    glm::ivec2 hi;
    if (!sample_range(b_lo, b_hi, band_, origin_, cell_size_, lo, hi)) {
        return {};
    }
    return apply(lo, hi, [&](glm::ivec2 s, std::int16_t old) {
        const glm::vec2 p = origin_ + cell_size_ * glm::vec2{s};
        return std::min(old, quantize(polygon_distance(vertices, p)));
    });
}

std::vector<glm::ivec2> SdfField2D::fill_from_mask(
        glm::ivec2 min_cell, glm::ivec2 size, CellPredicate<glm::ivec2> is_solid, int cells_per_mask_cell) {
    const int res = cells_per_mask_cell;
    if (size.x <= 0 || size.y <= 0 || res <= 0) {
        return {};
    }
    // Keep every sample index well inside int and the float grid.
    const auto fits = [](long long v) { return v > -kMaxIndex && v < kMaxIndex; };
    const long long r_lo_x = static_cast<long long>(min_cell.x) * res;
    const long long r_lo_y = static_cast<long long>(min_cell.y) * res;
    const long long r_hi_x = (static_cast<long long>(min_cell.x) + size.x) * res;
    const long long r_hi_y = (static_cast<long long>(min_cell.y) + size.y) * res;
    const int halo = static_cast<int>(std::ceil(band_ / cell_size_));
    if (!fits(r_lo_x - halo) || !fits(r_lo_y - halo) || !fits(r_hi_x + halo) || !fits(r_hi_y + halo)) {
        return {};
    }
    const glm::ivec2 rect_lo{static_cast<int>(r_lo_x), static_cast<int>(r_lo_y)};
    const glm::ivec2 rect_hi{static_cast<int>(r_hi_x), static_cast<int>(r_hi_y)};
    const glm::ivec2 lo = rect_lo - halo;
    const glm::ivec2 hi = rect_hi + halo;

    // The mask, once, over every mask cell any written sample can see.
    const glm::ivec2 m_lo{floor_div(lo.x - halo - 1, res) - 1, floor_div(lo.y - halo - 1, res) - 1};
    const glm::ivec2 m_hi{floor_div(hi.x + halo, res) + 1, floor_div(hi.y + halo, res) + 1};
    const int m_w = m_hi.x - m_lo.x + 1;
    const int m_h = m_hi.y - m_lo.y + 1;
    const bool outside_solid = outside_ == SdfOutside::Solid;
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(m_w) * static_cast<std::size_t>(m_h));
    for (int y = 0; y < m_h; ++y) {
        for (int x = 0; x < m_w; ++x) {
            const glm::ivec2 m{m_lo.x + x, m_lo.y + y};
            const bool in_rect =
                    m.x >= min_cell.x && m.y >= min_cell.y && m.x < min_cell.x + size.x && m.y < min_cell.y + size.y;
            mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_w) + static_cast<std::size_t>(x)] =
                    static_cast<std::uint8_t>(in_rect ? is_solid(m) : outside_solid);
        }
    }
    const auto solid = [&](glm::ivec2 m) {
        return mask[static_cast<std::size_t>(m.y - m_lo.y) * static_cast<std::size_t>(m_w) +
                    static_cast<std::size_t>(m.x - m_lo.x)] != 0;
    };
    const auto field_cell_solid = [&](int x, int y) { return solid({floor_div(x, res), floor_div(y, res)}); };

    const long long reach = halo; // in field cells
    const auto mask_distance = [&](glm::ivec2 s) {
        const int count = static_cast<int>(field_cell_solid(s.x - 1, s.y - 1)) + field_cell_solid(s.x, s.y - 1) +
                          field_cell_solid(s.x - 1, s.y) + field_cell_solid(s.x, s.y);
        if (count != 0 && count != 4) {
            return 0.0f;
        }
        const bool want_solid = count == 0;
        // Integer squared distance (in field cells) from the sample to the nearest wanted mask square.
        long long best = std::numeric_limits<long long>::max();
        const int mx0 = floor_div(s.x - static_cast<int>(reach), res) - 1;
        const int mx1 = floor_div(s.x + static_cast<int>(reach), res) + 1;
        const int my0 = floor_div(s.y - static_cast<int>(reach), res) - 1;
        const int my1 = floor_div(s.y + static_cast<int>(reach), res) + 1;
        for (int my = my0; my <= my1; ++my) {
            const long long y0 = static_cast<long long>(my) * res;
            const long long dy = std::max({y0 - s.y, 0LL, s.y - (y0 + res)});
            for (int mx = mx0; mx <= mx1; ++mx) {
                if (solid({mx, my}) != want_solid) {
                    continue;
                }
                const long long x0 = static_cast<long long>(mx) * res;
                const long long dx = std::max({x0 - s.x, 0LL, s.x - (x0 + res)});
                best = std::min(best, dx * dx + dy * dy);
            }
        }
        float d = band_;
        if (best != std::numeric_limits<long long>::max()) {
            d = std::min(band_, std::sqrt(static_cast<float>(best)) * cell_size_);
        }
        return want_solid ? d : -d;
    };

    const bool merge_min = outside_ == SdfOutside::Empty;
    return apply(lo, hi, [&](glm::ivec2 s, std::int16_t old) {
        const std::int16_t q = quantize(mask_distance(s));
        const bool in_rect = s.x >= rect_lo.x && s.y >= rect_lo.y && s.x <= rect_hi.x && s.y <= rect_hi.y;
        if (in_rect) {
            return q;
        }
        return merge_min ? std::min(old, q) : std::max(old, q);
    });
}

std::vector<glm::ivec2> SdfField2D::renormalize(glm::ivec2 chunk) {
    return renormalize(std::span<const glm::ivec2>{&chunk, 1});
}

std::vector<glm::ivec2> SdfField2D::renormalize(std::span<const glm::ivec2> chunks) {
    std::vector<glm::ivec2> list;
    for (const glm::ivec2 c : chunks) {
        if (has_chunk(c)) {
            list.push_back(c);
        }
    }
    std::sort(list.begin(), list.end(), row_major_less);
    list.erase(std::unique(list.begin(), list.end()), list.end());
    if (list.empty()) {
        return {};
    }

    const int halo = static_cast<int>(std::ceil(band_ / cell_size_)) + 1;
    const int w = kSdfChunkCells + 2 * halo;
    const int cells = w - 1;
    const float band_cells = band_ / cell_size_;
    const float limit2 = band_cells * band_cells;

    std::vector<std::array<std::int16_t, kSdfChunkSamples>> results(list.size());
    std::vector<float> values(static_cast<std::size_t>(w) * static_cast<std::size_t>(w));
    std::vector<Segment> segments;
    std::vector<std::uint32_t> cell_begin(static_cast<std::size_t>(cells) * static_cast<std::size_t>(cells) + 1);
    Cursor cursor{*this};

    for (std::size_t i = 0; i < list.size(); ++i) {
        const glm::ivec2 window = list[i] * kSdfChunkCells - halo;
        for (int y = 0; y < w; ++y) {
            for (int x = 0; x < w; ++x) {
                values[static_cast<std::size_t>(y * w + x)] = cursor.at({window.x + x, window.y + y});
            }
        }
        const auto v = [&](int x, int y) { return values[static_cast<std::size_t>(y * w + x)]; };
        segments.clear();
        for (int y = 0; y < cells; ++y) {
            for (int x = 0; x < cells; ++x) {
                cell_begin[static_cast<std::size_t>(y * cells + x)] = static_cast<std::uint32_t>(segments.size());
                Segment found[2];
                const int n = cell_segments(v(x, y), v(x + 1, y), v(x + 1, y + 1), v(x, y + 1), found);
                for (int k = 0; k < n; ++k) {
                    segments.push_back(found[k]);
                }
            }
        }
        cell_begin.back() = static_cast<std::uint32_t>(segments.size());

        std::array<std::int16_t, kSdfChunkSamples>& out = results[i];
        for (int ly = 0; ly < kSdfChunkCells; ++ly) {
            for (int lx = 0; lx < kSdfChunkCells; ++lx) {
                const int wx = lx + halo;
                const int wy = ly + halo;
                float best = limit2;
                for (int cy = wy - halo; cy < wy + halo; ++cy) {
                    for (int cx = wx - halo; cx < wx + halo; ++cx) {
                        const std::size_t cell = static_cast<std::size_t>(cy * cells + cx);
                        const std::uint32_t end = cell_begin[cell + 1];
                        if (cell_begin[cell] == end) {
                            continue;
                        }
                        // Relative to the sample, so the result does not depend on which window computes it.
                        const glm::vec2 offset{static_cast<float>(cx - wx), static_cast<float>(cy - wy)};
                        for (std::uint32_t k = cell_begin[cell]; k < end; ++k) {
                            best = std::min(best, segment_distance2(offset + segments[k].a, offset + segments[k].b));
                        }
                    }
                }
                const float d = best >= limit2 ? band_ : std::min(band_, std::sqrt(best) * cell_size_);
                out[static_cast<std::size_t>(ly * kSdfChunkCells + lx)] = quantize(v(wx, wy) < 0.0f ? -d : d);
            }
        }
    }

    DirtyTracker dirty;
    for (std::size_t i = 0; i < list.size(); ++i) {
        Chunk* chunk = find(list[i]);
        bool changed = false;
        bool column0 = false;
        bool row0 = false;
        bool corner = false;
        for (int k = 0; k < kSdfChunkSamples; ++k) {
            const std::int16_t next = results[i][static_cast<std::size_t>(k)];
            std::int16_t& slot = chunk->samples[static_cast<std::size_t>(k)];
            if (next == slot) {
                continue;
            }
            slot = next;
            changed = true;
            const int lx = k % kSdfChunkCells;
            const int ly = k / kSdfChunkCells;
            column0 = column0 || lx == 0;
            row0 = row0 || ly == 0;
            corner = corner || (lx == 0 && ly == 0);
        }
        if (changed) {
            dirty.add(list[i], column0, row0, corner);
        }
    }
    return finish(dirty);
}

}
