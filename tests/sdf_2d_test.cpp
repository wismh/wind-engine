#include <gtest/gtest.h>

#include <engine/ecs/sdf_2d.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace {

using engine::SdfField2D;
using engine::SdfField2DDesc;
using engine::SdfOutside;

SdfField2D make_field(float band, SdfOutside outside = SdfOutside::Empty, float cell_size = 1.0f) {
    SdfField2DDesc desc;
    desc.cell_size = cell_size;
    desc.band = band;
    desc.outside = outside;
    return SdfField2D{desc};
}

std::vector<glm::ivec2> coords(std::initializer_list<glm::ivec2> list) {
    return std::vector<glm::ivec2>(list);
}

float circle_sdf(glm::vec2 p, glm::vec2 c, float r) {
    return glm::length(p - c) - r;
}

float box_sdf(glm::vec2 p, glm::vec2 lo, glm::vec2 hi) {
    const glm::vec2 c = 0.5f * (lo + hi);
    const glm::vec2 h = 0.5f * (hi - lo);
    const glm::vec2 q = glm::abs(p - c) - h;
    return glm::length(glm::max(q, glm::vec2{0.0f})) + std::min(std::max(q.x, q.y), 0.0f);
}

std::array<glm::vec2, 4> box(glm::vec2 lo, glm::vec2 hi) {
    return {glm::vec2{lo.x, lo.y}, glm::vec2{hi.x, lo.y}, glm::vec2{hi.x, hi.y}, glm::vec2{lo.x, hi.y}};
}

// A deterministic spread of points.
std::vector<glm::vec2> points_around(glm::vec2 center, float extent, int count) {
    std::vector<glm::vec2> out;
    std::uint32_t state = 12345u;
    const auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
    };
    for (int i = 0; i < count; ++i) {
        out.push_back(center + extent * glm::vec2{2.0f * next() - 1.0f, 2.0f * next() - 1.0f});
    }
    return out;
}

TEST(SdfField2D, DescriptorIsSanitized) {
    SdfField2DDesc desc;
    desc.cell_size = -1.0f;
    desc.band = std::nanf("");
    const SdfField2D a{desc};
    EXPECT_FLOAT_EQ(a.cell_size(), 1.0f);
    EXPECT_FLOAT_EQ(a.band(), 4.0f);

    desc.cell_size = 2.0f;
    desc.band = 0.5f;
    const SdfField2D b{desc};
    EXPECT_FLOAT_EQ(b.band(), 2.0f);
}

TEST(SdfField2D, ChunkOfUsesFloorDivision) {
    EXPECT_EQ(SdfField2D::chunk_of({0, 0}), glm::ivec2(0, 0));
    EXPECT_EQ(SdfField2D::chunk_of({31, 32}), glm::ivec2(0, 1));
    EXPECT_EQ(SdfField2D::chunk_of({-1, -32}), glm::ivec2(-1, -1));
    EXPECT_EQ(SdfField2D::chunk_of({-33, 64}), glm::ivec2(-2, 2));
}

TEST(SdfField2D, OutsideDefaultEmptyAndSolid) {
    const SdfField2D empty = make_field(4.0f, SdfOutside::Empty);
    const SdfField2D solid = make_field(4.0f, SdfOutside::Solid);
    EXPECT_FLOAT_EQ(empty.sample({3.5f, -100.25f}), 4.0f);
    EXPECT_FLOAT_EQ(solid.sample({3.5f, -100.25f}), -4.0f);
    EXPECT_FLOAT_EQ(empty.sample_at({1000, -1000}), 4.0f);
    EXPECT_EQ(empty.chunk_count(), 0u);
    EXPECT_FLOAT_EQ(empty.sample({std::nanf(""), 0.0f}), 4.0f);
    EXPECT_FLOAT_EQ(solid.sample({1.0e30f, 0.0f}), -4.0f);
}

TEST(SdfField2D, QuantizationClampsAtBand) {
    const SdfField2D field = make_field(4.0f);
    EXPECT_EQ(field.quantize(4.0f), engine::kSdfQuantMax);
    EXPECT_EQ(field.quantize(100.0f), engine::kSdfQuantMax);
    EXPECT_EQ(field.quantize(-100.0f), -engine::kSdfQuantMax);
    EXPECT_EQ(field.quantize(std::numeric_limits<float>::infinity()), engine::kSdfQuantMax);
    EXPECT_EQ(field.quantize(0.0f), 0);
    const float step = 4.0f / 32767.0f;
    EXPECT_NEAR(field.dequantize(field.quantize(1.2345f)), 1.2345f, 0.5f * step + 1e-6f);

    SdfField2D filled = make_field(4.0f);
    filled.fill_circle({16.0f, 16.0f}, 12.0f);
    EXPECT_FLOAT_EQ(filled.sample({16.0f, 16.0f}), -4.0f);
    EXPECT_FLOAT_EQ(filled.sample_at({16, 16}), -4.0f);
    // A sample on the surface stores the exact distance within the quantization step.
    EXPECT_NEAR(filled.sample_at({16 + 12 + 1, 16}), 1.0f, step);
}

TEST(SdfField2D, SampleAndGradientMatchCircle) {
    SdfField2D field = make_field(4.0f);
    const glm::vec2 c{16.3f, 15.7f};
    const float r = 6.0f;
    field.fill_circle(c, r);
    int checked = 0;
    for (const glm::vec2 p : points_around(c, 10.0f, 400)) {
        const float exact = circle_sdf(p, c, r);
        if (std::abs(exact) > 2.5f || glm::length(p - c) < 1.0f) {
            continue;
        }
        ++checked;
        EXPECT_NEAR(field.sample(p), exact, 0.1f) << p.x << ", " << p.y;
        const glm::vec2 g = field.gradient(p);
        EXPECT_NEAR(glm::length(g), 1.0f, 1e-4f);
        EXPECT_GT(glm::dot(g, glm::normalize(p - c)), 0.98f) << p.x << ", " << p.y;
    }
    EXPECT_GT(checked, 50);
}

TEST(SdfField2D, SampleAndGradientMatchBox) {
    SdfField2D field = make_field(4.0f, SdfOutside::Empty, 0.5f);
    const glm::vec2 lo{2.2f, 3.1f};
    const glm::vec2 hi{9.7f, 7.4f};
    const auto verts = box(lo, hi);
    field.fill_polygon(verts);
    int checked = 0;
    for (const glm::vec2 p : points_around(0.5f * (lo + hi), 8.0f, 600)) {
        const float exact = box_sdf(p, lo, hi);
        if (std::abs(exact) > 3.0f) {
            continue;
        }
        ++checked;
        // Within a cell of the box's medial axis (inside) the bilinear cell mixes two faces.
        EXPECT_NEAR(field.sample(p), exact, exact < 0.0f ? 0.25f : 0.1f) << p.x << ", " << p.y;
    }
    EXPECT_GT(checked, 100);
    EXPECT_NEAR(field.gradient({12.0f, 5.0f}).x, 1.0f, 1e-3f);
    EXPECT_NEAR(field.gradient({5.0f, 1.0f}).y, -1.0f, 1e-3f);
}

TEST(SdfField2D, GradientIsZeroWhereFlat) {
    const SdfField2D field = make_field(4.0f);
    EXPECT_EQ(field.gradient({3.0f, 3.0f}), glm::vec2(0.0f));
}

TEST(SdfField2D, CarveInEmptySpaceChangesNothing) {
    SdfField2D field = make_field(4.0f);
    EXPECT_TRUE(field.carve_circle({16.0f, 16.0f}, 3.0f).empty());
    EXPECT_EQ(field.chunk_count(), 0u);
}

TEST(SdfField2D, CarveDirtiesOneChunk) {
    SdfField2D field = make_field(4.0f, SdfOutside::Solid);
    EXPECT_EQ(field.carve_circle({16.0f, 16.0f}, 3.0f), coords({{0, 0}}));
    EXPECT_EQ(field.chunk_coords(), coords({{0, 0}}));
    EXPECT_GT(field.sample({16.0f, 16.0f}), 2.9f);
    EXPECT_NEAR(field.sample({16.0f, 20.0f}), -1.0f, 1e-3f);
}

TEST(SdfField2D, CarveStraddlingTwoAndFourChunks) {
    SdfField2D two = make_field(4.0f, SdfOutside::Solid);
    EXPECT_EQ(two.carve_circle({32.0f, 16.0f}, 3.0f), coords({{0, 0}, {1, 0}}));
    SdfField2D four = make_field(4.0f, SdfOutside::Solid);
    EXPECT_EQ(four.carve_circle({32.0f, 32.0f}, 3.0f), coords({{0, 0}, {1, 0}, {0, 1}, {1, 1}}));
}

TEST(SdfField2D, CarveAtNegativeCoordinates) {
    SdfField2D field = make_field(4.0f, SdfOutside::Solid);
    EXPECT_EQ(field.carve_circle({-16.0f, -16.0f}, 3.0f), coords({{-1, -1}}));
    EXPECT_GT(field.sample({-16.0f, -16.0f}), 2.9f);
    SdfField2D corner = make_field(4.0f, SdfOutside::Solid);
    EXPECT_EQ(corner.carve_circle({0.0f, 0.0f}, 2.0f), coords({{-1, -1}, {0, -1}, {-1, 0}, {0, 0}}));
}

TEST(SdfField2D, ChangedFirstColumnDirtiesTheChunkToTheLeft) {
    // Changes reach |p - c| < radius + band = 2: x = 33..35 only, all inside chunk 1.
    SdfField2D inside = make_field(1.5f, SdfOutside::Solid);
    EXPECT_EQ(inside.carve_circle({34.0f, 16.0f}, 0.5f), coords({{1, 0}}));
    // x = 32 changes: cell 31 of chunk 0 reads it, so chunk 0 is dirty (and allocated) too.
    SdfField2D seam = make_field(1.5f, SdfOutside::Solid);
    EXPECT_EQ(seam.carve_circle({33.9f, 16.0f}, 0.5f), coords({{0, 0}, {1, 0}}));
    EXPECT_TRUE(seam.has_chunk({0, 0}));
    EXPECT_EQ(seam.revision({0, 0}), 1u);
    // Nothing in chunk 0 itself changed.
    for (const std::int16_t q : seam.chunk_samples({0, 0})) {
        ASSERT_EQ(q, -engine::kSdfQuantMax);
    }
}

TEST(SdfField2D, FillDirtiesChunksExactly) {
    SdfField2D field = make_field(4.0f);
    EXPECT_EQ(field.fill_circle({48.0f, -16.0f}, 5.0f), coords({{1, -1}}));
    const auto verts = box({60.0f, -40.0f}, {70.0f, -20.0f});
    EXPECT_EQ(field.fill_polygon(verts), coords({{1, -2}, {2, -2}, {1, -1}, {2, -1}}));
}

TEST(SdfField2D, RevisionsBumpOnlyTouchedChunks) {
    SdfField2D field = make_field(4.0f, SdfOutside::Solid);
    EXPECT_EQ(field.revision({0, 0}), 0u);
    field.carve_circle({16.0f, 16.0f}, 3.0f);
    EXPECT_EQ(field.revision({0, 0}), 1u);
    field.carve_circle({80.0f, 16.0f}, 3.0f);
    EXPECT_EQ(field.revision({0, 0}), 1u);
    EXPECT_EQ(field.revision({2, 0}), 1u);
    EXPECT_EQ(field.revision({1, 0}), 0u);
    // The same carve again changes nothing.
    EXPECT_TRUE(field.carve_circle({16.0f, 16.0f}, 3.0f).empty());
    EXPECT_EQ(field.revision({0, 0}), 1u);
    field.carve_circle({18.0f, 16.0f}, 3.0f);
    EXPECT_EQ(field.revision({0, 0}), 2u);
    EXPECT_EQ(field.revision({2, 0}), 1u);
}

TEST(SdfField2D, InvalidShapesAreNoOps) {
    SdfField2D field = make_field(4.0f, SdfOutside::Solid);
    EXPECT_TRUE(field.carve_circle({0.0f, 0.0f}, 0.0f).empty());
    EXPECT_TRUE(field.carve_circle({0.0f, 0.0f}, -1.0f).empty());
    EXPECT_TRUE(field.carve_circle({std::nanf(""), 0.0f}, 1.0f).empty());
    const std::array<glm::vec2, 2> two{glm::vec2{0.0f, 0.0f}, glm::vec2{1.0f, 0.0f}};
    EXPECT_TRUE(field.carve_polygon(two).empty());
    auto clockwise = box({0.0f, 0.0f}, {4.0f, 4.0f});
    std::reverse(clockwise.begin(), clockwise.end());
    EXPECT_TRUE(field.carve_polygon(clockwise).empty());
    const std::array<glm::vec2, 4> repeated{
            glm::vec2{0.0f, 0.0f}, glm::vec2{4.0f, 0.0f}, glm::vec2{4.0f, 0.0f}, glm::vec2{0.0f, 4.0f}};
    EXPECT_TRUE(field.carve_polygon(repeated).empty());
    const std::array<glm::vec2, 4> concave{
            glm::vec2{0.0f, 0.0f}, glm::vec2{4.0f, 0.0f}, glm::vec2{1.0f, 1.0f}, glm::vec2{0.0f, 4.0f}};
    EXPECT_TRUE(field.carve_polygon(concave).empty());
    EXPECT_EQ(field.chunk_count(), 0u);
    // A collinear vertex is kept.
    const std::array<glm::vec2, 4> collinear{
            glm::vec2{10.0f, 10.0f}, glm::vec2{14.0f, 10.0f}, glm::vec2{18.0f, 10.0f}, glm::vec2{14.0f, 14.0f}};
    EXPECT_FALSE(field.carve_polygon(collinear).empty());
}

TEST(SdfField2D, CarvedPolygonIsEmptyInside) {
    SdfField2D field = make_field(4.0f, SdfOutside::Solid);
    const auto verts = box({4.0f, 4.0f}, {12.0f, 10.0f});
    field.carve_polygon(verts);
    EXPECT_NEAR(field.sample({8.0f, 7.0f}), 3.0f, 1e-3f);
    EXPECT_NEAR(field.sample({8.0f, 12.0f}), -2.0f, 1e-3f);
    EXPECT_NEAR(field.sample({8.0f, 10.0f}), 0.0f, 1e-3f);
}

TEST(SdfField2D, FillFromMaskGivesTrueDistances) {
    SdfField2D field = make_field(4.0f);
    // A 4 x 4 block of solid cells at [8, 12) x [8, 12).
    const auto block = [](glm::ivec2 c) { return c.x >= 8 && c.x < 12 && c.y >= 8 && c.y < 12; };
    const auto dirty = field.fill_from_mask({0, 0}, {32, 32}, block);
    EXPECT_EQ(dirty, coords({{0, 0}}));
    EXPECT_NEAR(field.sample_at({6, 10}), 2.0f, 1e-3f);
    EXPECT_NEAR(field.sample_at({10, 10}), -2.0f, 1e-3f);
    EXPECT_NEAR(field.sample_at({9, 10}), -1.0f, 1e-3f);
    EXPECT_NEAR(field.sample_at({8, 8}), 0.0f, 1e-3f);
    EXPECT_NEAR(field.sample_at({8, 10}), 0.0f, 1e-3f);
    EXPECT_NEAR(field.sample_at({6, 6}), std::sqrt(8.0f), 1e-3f);
    EXPECT_FLOAT_EQ(field.sample_at({0, 0}), 4.0f);
    EXPECT_NEAR(field.sample({7.5f, 10.0f}), 0.5f, 1e-3f);
}

TEST(SdfField2D, FillFromMaskWithSubdividedCells) {
    // A one-tile platform at tile row 5, tiles x in [0, 10); two field cells per tile.
    SdfField2D field = make_field(2.0f, SdfOutside::Empty, 0.5f);
    const auto platform = [](glm::ivec2 c) { return c.y == 5 && c.x >= 0 && c.x < 10; };
    field.fill_from_mask({-4, 0}, {20, 12}, platform, 2);
    EXPECT_NEAR(field.sample({5.0f, 5.5f}), -0.5f, 1e-3f);
    EXPECT_NEAR(field.sample({5.0f, 7.0f}), 1.0f, 1e-3f);
    EXPECT_NEAR(field.sample({5.0f, 4.0f}), 1.0f, 1e-3f);
    EXPECT_NEAR(field.sample({11.0f, 5.5f}), 1.0f, 1e-3f);
    const auto hit = field.raymarch({5.0f, 9.0f}, {0.0f, -1.0f}, 10.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->point.y, 6.0f, 1e-3f);
    EXPECT_NEAR(hit->normal.y, 1.0f, 1e-3f);
}

TEST(SdfField2D, FillFromMaskMergesItsMargin) {
    SdfField2D field = make_field(4.0f);
    field.fill_circle({-3.0f, 8.0f}, 2.0f);
    const float before = field.sample_at({-2, 8});
    // An empty mask to the right does not erase the circle in its margin.
    field.fill_from_mask({0, 0}, {16, 16}, [](glm::ivec2) { return false; });
    EXPECT_FLOAT_EQ(field.sample_at({-2, 8}), before);
}

TEST(SdfField2D, RaymarchHitsAndMisses) {
    SdfField2D field = make_field(4.0f);
    field.fill_circle({16.0f, 16.0f}, 5.0f);
    const auto hit = field.raymarch({0.0f, 16.0f}, {2.0f, 0.0f}, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 11.0f, 0.05f);
    EXPECT_NEAR(hit->point.x, 11.0f, 0.05f);
    EXPECT_NEAR(std::abs(field.sample(hit->point)), 0.0f, 1e-3f);
    EXPECT_NEAR(hit->normal.x, -1.0f, 1e-2f);

    EXPECT_FALSE(field.raymarch({0.0f, 30.0f}, {1.0f, 0.0f}, 100.0f).has_value());
    EXPECT_FALSE(field.raymarch({0.0f, 16.0f}, {1.0f, 0.0f}, 10.0f).has_value());
    EXPECT_FALSE(field.raymarch({0.0f, 16.0f}, {-1.0f, 0.0f}, 1000.0f).has_value());
    EXPECT_FALSE(field.raymarch({0.0f, 16.0f}, {0.0f, 0.0f}, 100.0f).has_value());
    EXPECT_FALSE(field.raymarch({0.0f, 16.0f}, {1.0f, 0.0f}, -1.0f).has_value());
    EXPECT_FALSE(field.raymarch({0.0f, 16.0f}, {1.0f, 0.0f}, std::numeric_limits<float>::infinity()).has_value());
}

TEST(SdfField2D, RaymarchGrazing) {
    SdfField2D field = make_field(4.0f);
    field.fill_circle({16.0f, 16.0f}, 5.0f);
    EXPECT_FALSE(field.raymarch({0.0f, 21.5f}, {1.0f, 0.0f}, 100.0f).has_value());
    const auto hit = field.raymarch({0.0f, 20.5f}, {1.0f, 0.0f}, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->point.x, 16.0f - std::sqrt(25.0f - 4.5f * 4.5f), 0.1f);
}

TEST(SdfField2D, RaymarchAgainstBoxAndFromInside) {
    SdfField2D field = make_field(4.0f);
    const auto verts = box({10.0f, 10.0f}, {20.0f, 20.0f});
    field.fill_polygon(verts);
    const auto down = field.raymarch({15.0f, 300.0f}, {0.0f, -1.0f}, 1000.0f);
    ASSERT_TRUE(down.has_value());
    EXPECT_NEAR(down->distance, 280.0f, 1e-2f);
    EXPECT_NEAR(down->normal.y, 1.0f, 1e-3f);
    const auto inside = field.raymarch({15.0f, 15.0f}, {1.0f, 0.0f}, 10.0f);
    ASSERT_TRUE(inside.has_value());
    EXPECT_FLOAT_EQ(inside->distance, 0.0f);
    EXPECT_EQ(inside->point, glm::vec2(15.0f, 15.0f));
}

TEST(SdfField2D, RaymarchInSolidWorld) {
    SdfField2D field = make_field(4.0f, SdfOutside::Solid);
    field.carve_circle({16.0f, 16.0f}, 6.0f);
    const auto hit = field.raymarch({16.0f, 16.0f}, {0.0f, 1.0f}, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 6.0f, 0.05f);
    EXPECT_NEAR(hit->normal.y, -1.0f, 1e-2f);
    EXPECT_FLOAT_EQ(field.raymarch({100.0f, 100.0f}, {1.0f, 0.0f}, 5.0f)->distance, 0.0f);
}

TEST(SdfField2D, SweepCircleHitsFloor) {
    SdfField2D field = make_field(4.0f);
    const auto floor = box({0.0f, 0.0f}, {64.0f, 10.0f});
    field.fill_polygon(floor);
    const auto hit = field.sweep_circle({20.0f, 15.0f}, 1.0f, {0.0f, -10.0f});
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->fraction, 0.4f, 1e-3f);
    EXPECT_NEAR(hit->center.y, 11.0f, 1e-2f);
    EXPECT_NEAR(hit->normal.y, 1.0f, 1e-3f);
    EXPECT_NEAR(hit->point.y, 10.0f, 1e-2f);
    EXPECT_FLOAT_EQ(hit->depth, 0.0f);
    // Not penetrating at the reported fraction.
    EXPECT_GE(field.sample(hit->center) - 1.0f, 0.0f);

    EXPECT_FALSE(field.sweep_circle({20.0f, 15.0f}, 1.0f, {5.0f, 0.0f}).has_value());
    EXPECT_FALSE(field.sweep_circle({20.0f, 15.0f}, 1.0f, {0.0f, -2.0f}).has_value());
    EXPECT_FALSE(field.sweep_circle({20.0f, 15.0f}, 1.0f, {0.0f, 0.0f}).has_value());
    EXPECT_FALSE(field.sweep_circle({20.0f, 15.0f}, 4.0f, {0.0f, -10.0f}).has_value());
    EXPECT_FALSE(field.sweep_circle({20.0f, 15.0f}, -1.0f, {0.0f, -10.0f}).has_value());
}

TEST(SdfField2D, SweepCircleStartingInside) {
    SdfField2D field = make_field(4.0f);
    const auto floor = box({0.0f, 0.0f}, {64.0f, 10.0f});
    field.fill_polygon(floor);
    const auto hit = field.sweep_circle({20.0f, 10.5f}, 1.0f, {0.0f, 0.0f});
    ASSERT_TRUE(hit.has_value());
    EXPECT_FLOAT_EQ(hit->fraction, 0.0f);
    EXPECT_NEAR(hit->depth, 0.5f, 1e-3f);
    EXPECT_NEAR(hit->normal.y, 1.0f, 1e-3f);
    EXPECT_NEAR(hit->point.y, 10.0f, 1e-3f);
}

TEST(SdfField2D, SweepCircleDiagonalAgainstCircle) {
    SdfField2D field = make_field(4.0f);
    field.fill_circle({30.0f, 30.0f}, 4.0f);
    const auto hit = field.sweep_circle({10.0f, 10.0f}, 1.5f, {20.0f, 20.0f});
    ASSERT_TRUE(hit.has_value());
    const float travel = glm::length(glm::vec2{20.0f, 20.0f}) - 5.5f;
    EXPECT_NEAR(hit->fraction, travel / glm::length(glm::vec2{20.0f, 20.0f}), 5e-3f);
    EXPECT_NEAR(hit->normal.x, -std::sqrt(0.5f), 1e-2f);
}

// Two circles carved from a solid world overlap in a lens; CSG leaves a distance bound at the center.
SdfField2D lens(glm::vec2 center) {
    SdfField2D field = make_field(6.0f, SdfOutside::Solid);
    field.carve_circle(center - glm::vec2{3.0f, 0.0f}, 5.0f);
    field.carve_circle(center + glm::vec2{3.0f, 0.0f}, 5.0f);
    return field;
}

TEST(SdfField2D, RenormalizeRestoresDistance) {
    SdfField2D field = lens({0.0f, 0.0f});
    // CSG: the distance to the nearer circle boundary; the true distance is to the cusps at (0, +-4).
    EXPECT_NEAR(field.sample_at({0, 0}), 2.0f, 1e-3f);
    const auto dirty = field.renormalize(field.chunk_coords());
    EXPECT_FALSE(dirty.empty());
    EXPECT_NEAR(field.sample_at({0, 0}), 4.0f, 0.25f);
    // Away from the cusps distances are already right and stay so.
    EXPECT_NEAR(field.sample_at({-7, 0}), 1.0f, 0.05f);
    EXPECT_NEAR(field.sample_at({-10, 0}), -2.0f, 0.05f);
    // Near the surface the field is a distance: |grad| ~ 1 where the central difference does not straddle a cusp.
    int checked = 0;
    for (int y = -10; y <= 10; ++y) {
        for (int x = -12; x <= 12; ++x) {
            if (std::abs(x) <= 2) {
                continue;
            }
            const float d = field.sample_at({x, y});
            if (std::abs(d) > 1.5f) {
                continue;
            }
            const float gx = 0.5f * (field.sample_at({x + 1, y}) - field.sample_at({x - 1, y}));
            const float gy = 0.5f * (field.sample_at({x, y + 1}) - field.sample_at({x, y - 1}));
            ++checked;
            EXPECT_NEAR(std::sqrt(gx * gx + gy * gy), 1.0f, 0.15f) << x << ", " << y;
        }
    }
    EXPECT_GT(checked, 30);
}

TEST(SdfField2D, RenormalizeIsSeamContinuous) {
    // The same lens across four chunk corners and in the middle of one chunk: bit-identical samples.
    SdfField2D seam = lens({0.0f, 0.0f});
    SdfField2D middle = lens({80.0f, 80.0f});
    EXPECT_EQ(middle.chunk_coords(), coords({{2, 2}}));
    EXPECT_EQ(seam.chunk_coords().size(), 4u);
    seam.renormalize(seam.chunk_coords());
    middle.renormalize(middle.chunk_coords());
    for (int y = -20; y <= 20; ++y) {
        for (int x = -20; x <= 20; ++x) {
            ASSERT_EQ(seam.sample_at({x, y}), middle.sample_at({x + 80, y + 80})) << x << ", " << y;
        }
    }
}

TEST(SdfField2D, RenormalizeDoesNotDependOnOrder) {
    SdfField2D a = lens({0.0f, 0.0f});
    SdfField2D b = lens({0.0f, 0.0f});
    std::vector<glm::ivec2> list = a.chunk_coords();
    a.renormalize(list);
    std::reverse(list.begin(), list.end());
    list.push_back(list.front());
    b.renormalize(list);
    for (const glm::ivec2 c : a.chunk_coords()) {
        const auto sa = a.chunk_samples(c);
        const auto sb = b.chunk_samples(c);
        ASSERT_TRUE(std::equal(sa.begin(), sa.end(), sb.begin(), sb.end()));
    }
    // Renormalizing again is nearly a fixed point: the interface moves by rounding only.
    const SdfField2D before = a;
    a.renormalize(a.chunk_coords());
    for (int y = -16; y <= 16; ++y) {
        for (int x = -16; x <= 16; ++x) {
            EXPECT_NEAR(a.sample_at({x, y}), before.sample_at({x, y}), 0.05f) << x << ", " << y;
        }
    }
}

TEST(SdfField2D, RenormalizeAfterManyOverlappingCarves) {
    SdfField2D field = make_field(4.0f, SdfOutside::Solid);
    std::vector<glm::ivec2> dirty;
    for (int i = 0; i < 24; ++i) {
        const float a = 0.4f * static_cast<float>(i);
        const auto d = field.carve_circle({32.0f + 10.0f * std::cos(a), 32.0f + 8.0f * std::sin(a)}, 3.5f);
        dirty.insert(dirty.end(), d.begin(), d.end());
    }
    field.renormalize(dirty);
    int checked = 0;
    int good = 0;
    for (int y = 10; y <= 54; ++y) {
        for (int x = 10; x <= 54; ++x) {
            if (std::abs(field.sample_at({x, y})) > 1.0f) {
                continue;
            }
            const float gx = 0.5f * (field.sample_at({x + 1, y}) - field.sample_at({x - 1, y}));
            const float gy = 0.5f * (field.sample_at({x, y + 1}) - field.sample_at({x, y - 1}));
            ++checked;
            good += std::abs(std::sqrt(gx * gx + gy * gy) - 1.0f) < 0.2f ? 1 : 0;
        }
    }
    EXPECT_GT(checked, 100);
    EXPECT_GT(good, checked * 9 / 10);
    // Unallocated chunks are a no-op.
    EXPECT_TRUE(field.renormalize(glm::ivec2{40, 40}).empty());
}

TEST(SdfField2D, SameOpsGiveBitIdenticalSamples) {
    const auto run = [] {
        SdfField2D field = make_field(3.0f, SdfOutside::Solid, 0.25f);
        field.carve_circle({1.3f, 2.7f}, 2.1f);
        const std::array<glm::vec2, 3> tri{glm::vec2{-3.0f, -1.0f}, glm::vec2{4.0f, -2.0f}, glm::vec2{0.5f, 5.0f}};
        field.carve_polygon(tri);
        field.fill_circle({0.0f, 0.0f}, 1.0f);
        field.renormalize(field.chunk_coords());
        return field;
    };
    const SdfField2D a = run();
    const SdfField2D b = run();
    ASSERT_EQ(a.chunk_coords(), b.chunk_coords());
    for (const glm::ivec2 c : a.chunk_coords()) {
        const auto sa = a.chunk_samples(c);
        const auto sb = b.chunk_samples(c);
        ASSERT_TRUE(std::equal(sa.begin(), sa.end(), sb.begin(), sb.end()));
        EXPECT_EQ(a.revision(c), b.revision(c));
    }
}

}

TEST(SdfField2D, EditCountFollowsEditsThatChangeSamples) {
    SdfField2D field = make_field(4.0f);
    EXPECT_EQ(field.edit_count(), 0u);
    EXPECT_TRUE(field.carve_circle({5.0f, 5.0f}, 2.0f).empty());
    EXPECT_EQ(field.edit_count(), 0u);  // carving empty space changes nothing
    ASSERT_FALSE(field.fill_circle({5.0f, 5.0f}, 2.0f).empty());
    EXPECT_EQ(field.edit_count(), 1u);
    EXPECT_TRUE(field.fill_circle({5.0f, 5.0f}, 2.0f).empty());
    EXPECT_EQ(field.edit_count(), 1u);
    (void)field.carve_circle({5.0f, 5.0f}, 1.0f);
    EXPECT_EQ(field.edit_count(), 2u);
}

TEST(SdfField2D, AssignChunkCopiesSamplesAndDirtiesNeighbours) {
    SdfField2D source = make_field(4.0f);
    (void)source.fill_circle({40.0f, 40.0f}, 6.0f);
    const std::span<const std::int16_t> samples = source.chunk_samples({1, 1});
    ASSERT_EQ(samples.size(), static_cast<std::size_t>(engine::kSdfChunkSamples));

    SdfField2D copy = make_field(4.0f);
    EXPECT_TRUE(copy.assign_chunk({1, 1}, samples.first(10)).empty());
    EXPECT_FALSE(copy.has_chunk({1, 1}));
    const std::vector<glm::ivec2> dirty = copy.assign_chunk({1, 1}, samples);
    ASSERT_FALSE(dirty.empty());
    EXPECT_TRUE(std::ranges::find(dirty, glm::ivec2{1, 1}) != dirty.end());
    EXPECT_EQ(copy.revision({1, 1}), 1u);
    EXPECT_EQ(copy.edit_count(), 1u);
    const std::span<const std::int16_t> copied = copy.chunk_samples({1, 1});
    EXPECT_TRUE(std::equal(samples.begin(), samples.end(), copied.begin()));
    EXPECT_TRUE(copy.assign_chunk({1, 1}, samples).empty());

    // An all-outside chunk is still allocated and reported once.
    std::vector<std::int16_t> empty(static_cast<std::size_t>(engine::kSdfChunkSamples), engine::kSdfQuantMax);
    EXPECT_EQ(copy.assign_chunk({-4, 2}, empty), std::vector<glm::ivec2>{glm::ivec2(-4, 2)});
    EXPECT_TRUE(copy.has_chunk({-4, 2}));
    EXPECT_EQ(copy.revision({-4, 2}), 1u);
}

TEST(SdfField2D, SampleManyMatchesSample) {
    SdfField2D field = make_field(3.0f, SdfOutside::Solid, 0.5f);
    (void)field.carve_circle({15.0f, 15.0f}, 9.0f);
    std::vector<glm::vec2> points;
    for (int i = 0; i < 200; ++i) {
        const float t = static_cast<float>(i);
        points.push_back({0.37f * t - 10.0f, 15.0f + 6.0f * std::sin(0.1f * t)});
    }
    points.push_back({std::numeric_limits<float>::quiet_NaN(), 0.0f});
    std::vector<float> out(points.size() + 3, -7.0f);
    field.sample_many(points, out);
    for (std::size_t i = 0; i < points.size(); ++i) {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(out[i]), std::bit_cast<std::uint32_t>(field.sample(points[i]))) << i;
    }
    EXPECT_EQ(out.back(), -7.0f);
}
