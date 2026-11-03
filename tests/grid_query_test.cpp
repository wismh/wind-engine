#include <gtest/gtest.h>

#include <engine/ecs/grid_query.h>
#include <engine/render/tilemap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace {

constexpr float kEpsilon = 1e-4f;

struct Grid3 {
    std::set<std::array<int, 3>> cells;

    void add(int x, int y, int z) {
        cells.insert({x, y, z});
    }
    bool operator()(glm::ivec3 c) const {
        return cells.contains({c.x, c.y, c.z});
    }
};

struct Grid2 {
    std::set<std::array<int, 2>> cells;

    void add(int x, int y) {
        cells.insert({x, y});
    }
    bool operator()(glm::ivec2 c) const {
        return cells.contains({c.x, c.y});
    }
};

// A floor: cells y = 0 under x, z in [-10, 10].
Grid3 floor_grid() {
    Grid3 grid;
    for (int x = -10; x <= 10; ++x) {
        for (int z = -10; z <= 10; ++z) {
            grid.add(x, 0, z);
        }
    }
    return grid;
}

// A wall: cells x = `x` for y in [1, top], z in [-10, 10].
void add_wall(Grid3& grid, int x, int top) {
    for (int y = 1; y <= top; ++y) {
        for (int z = -10; z <= 10; ++z) {
            grid.add(x, y, z);
        }
    }
}

// A 0.6 x 1.8 x 0.6 player box with its feet at `feet_y`.
engine::render::Aabb player_box(float min_x, float feet_y, float min_z = 0.2f) {
    return engine::render::Aabb{{min_x, feet_y, min_z}, {min_x + 0.6f, feet_y + 1.8f, min_z + 0.6f}};
}

bool never_solid(glm::ivec3) {
    return false;
}

void expect_vec_near(glm::vec3 actual, glm::vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, kEpsilon);
    EXPECT_NEAR(actual.y, expected.y, kEpsilon);
    EXPECT_NEAR(actual.z, expected.z, kEpsilon);
}

}

// --- raycast_grid (3D) ---

TEST(GridRay, HitsAlongEachAxis) {
    const glm::vec3 origin{0.5f, 0.5f, 0.5f};
    const std::array<glm::ivec3, 6> axes{glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, 1, 0},
            glm::ivec3{0, -1, 0}, glm::ivec3{0, 0, 1}, glm::ivec3{0, 0, -1}};
    for (const glm::ivec3 axis : axes) {
        Grid3 grid;
        grid.add(axis.x * 3, axis.y * 3, axis.z * 3);
        const auto hit = engine::raycast_grid(engine::Ray{origin, glm::vec3{axis}}, 10.0f, grid);
        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->cell, axis * 3);
        EXPECT_EQ(hit->normal, -axis);
        EXPECT_NEAR(hit->t, 2.5f, kEpsilon);
        expect_vec_near(hit->point, origin + 2.5f * glm::vec3{axis});
    }
}

TEST(GridRay, HitsAlongDiagonals) {
    {
        // Exactly through the corners at x = y = 1 and x = y = 2: the diagonal cell is entered through x first.
        Grid3 grid;
        grid.add(2, 2, 0);
        const auto hit = engine::raycast_grid(engine::Ray{{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}}, 10.0f, grid);
        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->cell, glm::ivec3(2, 2, 0));
        EXPECT_EQ(hit->normal, glm::ivec3(-1, 0, 0));
        EXPECT_NEAR(hit->t, 1.5f, kEpsilon);
        expect_vec_near(hit->point, {2.0f, 2.0f, 0.5f});
    }
    {
        // A shallow diagonal: (1,0) at 0.5, (2,0) at 1.5, (2,1) through its bottom face at 1.875.
        Grid3 grid;
        grid.add(2, 1, 0);
        const auto hit = engine::raycast_grid(engine::Ray{{0.5f, 0.25f, 0.5f}, {1.0f, 0.4f, 0.0f}}, 10.0f, grid);
        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->cell, glm::ivec3(2, 1, 0));
        EXPECT_EQ(hit->normal, glm::ivec3(0, -1, 0));
        EXPECT_NEAR(hit->t, 1.875f, kEpsilon);
        expect_vec_near(hit->point, {2.375f, 1.0f, 0.5f});
    }
    {
        // A full 3D diagonal toward negative coordinates.
        Grid3 grid;
        grid.add(-2, -2, -2);
        const auto hit = engine::raycast_grid(engine::Ray{{0.5f, 0.5f, 0.5f}, {-1.0f, -1.0f, -1.0f}}, 10.0f, grid);
        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->cell, glm::ivec3(-2, -2, -2));
        EXPECT_EQ(hit->normal, glm::ivec3(1, 0, 0));
        EXPECT_NEAR(hit->t, 1.5f, kEpsilon);
        expect_vec_near(hit->point, {-1.0f, -1.0f, -1.0f});
    }
}

TEST(GridRay, StartInsideSolidReportsZero) {
    Grid3 grid;
    grid.add(0, 0, 0);
    const glm::vec3 origin{0.5f, 0.25f, 0.75f};
    for (const glm::vec3 direction : {glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f}, glm::vec3{-1.0f, -2.0f, 3.0f}}) {
        for (const float max_distance : {0.0f, 5.0f}) {
            const auto hit = engine::raycast_grid(engine::Ray{origin, direction}, max_distance, grid);
            ASSERT_TRUE(hit);
            EXPECT_EQ(hit->cell, glm::ivec3(0, 0, 0));
            EXPECT_EQ(hit->normal, glm::ivec3(0, 0, 0));
            EXPECT_EQ(hit->t, 0.0f);
            EXPECT_EQ(hit->point, origin);
        }
    }
}

TEST(GridRay, StartOnFaceGoingAwayMisses) {
    Grid3 grid;
    grid.add(2, 0, 0);
    const glm::vec3 on_face{2.0f, 0.5f, 0.5f};
    EXPECT_FALSE(engine::raycast_grid(engine::Ray{on_face, {-1.0f, 0.0f, 0.0f}}, 10.0f, grid));

    const auto into = engine::raycast_grid(engine::Ray{on_face, {1.0f, 0.0f, 0.0f}}, 10.0f, grid);
    ASSERT_TRUE(into);
    EXPECT_EQ(into->cell, glm::ivec3(2, 0, 0));
    EXPECT_EQ(into->t, 0.0f);
}

TEST(GridRay, RespectsMaxDistance) {
    Grid3 grid;
    grid.add(3, 0, 0);
    const engine::Ray ray{{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}};
    EXPECT_FALSE(engine::raycast_grid(ray, 2.4f, grid));
    const auto exact = engine::raycast_grid(ray, 2.5f, grid);
    ASSERT_TRUE(exact);
    EXPECT_EQ(exact->t, 2.5f);
    EXPECT_TRUE(engine::raycast_grid(ray, 10.0f, grid));
    EXPECT_TRUE(engine::raycast_grid(ray, std::numeric_limits<float>::infinity(), grid));
}

TEST(GridRay, ZeroOrInvalidInputMisses) {
    Grid3 grid;
    grid.add(3, 0, 0);
    grid.add(0, 0, 0);
    const glm::vec3 outside{0.5f, 1.5f, 0.5f};
    EXPECT_FALSE(engine::raycast_grid(engine::Ray{outside, glm::vec3{0.0f}}, 10.0f, grid));
    EXPECT_FALSE(engine::raycast_grid(engine::Ray{{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}}, -1.0f, grid));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(engine::raycast_grid(engine::Ray{{nan, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}}, 10.0f, grid));
    EXPECT_FALSE(engine::raycast_grid(engine::Ray{outside, {nan, 0.0f, 0.0f}}, 10.0f, grid));
    EXPECT_FALSE(engine::raycast_grid(engine::Ray{outside, {1.0f, 0.0f, 0.0f}}, nan, grid));
}

TEST(GridRay, NonUnitDirectionScalesT) {
    Grid3 grid;
    grid.add(3, 0, 0);
    const auto hit = engine::raycast_grid(engine::Ray{{0.5f, 0.5f, 0.5f}, {2.0f, 0.0f, 0.0f}}, 10.0f, grid);
    ASSERT_TRUE(hit);
    EXPECT_NEAR(hit->t, 1.25f, kEpsilon);
    expect_vec_near(hit->point, {3.0f, 0.5f, 0.5f});
}

TEST(GridRay, CellPlusNormalIsPlacementCell) {
    Grid3 grid;
    grid.add(0, 0, 0);
    const auto hit = engine::raycast_grid(engine::Ray{{0.5f, 5.5f, 0.5f}, {0.0f, -1.0f, 0.0f}}, 10.0f, grid);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->normal, glm::ivec3(0, 1, 0));
    EXPECT_EQ(hit->cell + hit->normal, glm::ivec3(0, 1, 0));
    EXPECT_NEAR(hit->t, 4.5f, kEpsilon);
}

TEST(GridRay, ExactEdgeIsBlockedBySideCell) {
    const engine::Ray ray{{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}};
    struct Case {
        glm::ivec3 solid;
        glm::ivec3 normal;
    };
    for (const Case c : {Case{{1, 0, 0}, {-1, 0, 0}}, Case{{0, 1, 0}, {0, -1, 0}}, Case{{1, 1, 0}, {-1, 0, 0}}}) {
        Grid3 grid;
        grid.add(c.solid.x, c.solid.y, c.solid.z);
        const auto hit = engine::raycast_grid(ray, 5.0f, grid);
        ASSERT_TRUE(hit) << c.solid.x << c.solid.y;
        EXPECT_EQ(hit->cell, c.solid);
        EXPECT_EQ(hit->normal, c.normal);
        EXPECT_NEAR(hit->t, 0.5f, kEpsilon);
        expect_vec_near(hit->point, {1.0f, 1.0f, 0.5f});
    }
    EXPECT_FALSE(engine::raycast_grid(ray, 5.0f, Grid3{}));
}

TEST(GridRay, ExactCornerChecksAllNeighbours) {
    const engine::Ray ray{{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}};
    struct Case {
        glm::ivec3 solid;
        glm::ivec3 normal;
    };
    const std::array<Case, 7> cases{Case{{1, 0, 0}, {-1, 0, 0}}, Case{{0, 1, 0}, {0, -1, 0}},
            Case{{0, 0, 1}, {0, 0, -1}}, Case{{1, 1, 0}, {-1, 0, 0}}, Case{{1, 0, 1}, {-1, 0, 0}},
            Case{{0, 1, 1}, {0, -1, 0}}, Case{{1, 1, 1}, {-1, 0, 0}}};
    for (const Case& c : cases) {
        Grid3 grid;
        grid.add(c.solid.x, c.solid.y, c.solid.z);
        const auto hit = engine::raycast_grid(ray, 5.0f, grid);
        ASSERT_TRUE(hit) << c.solid.x << c.solid.y << c.solid.z;
        EXPECT_EQ(hit->cell, c.solid);
        EXPECT_EQ(hit->normal, c.normal);
        EXPECT_NEAR(hit->t, 0.5f, kEpsilon);
    }
    // Side cells are tested before the diagonal one.
    Grid3 both;
    both.add(0, 1, 0);
    both.add(1, 1, 1);
    const auto hit = engine::raycast_grid(ray, 5.0f, both);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->cell, glm::ivec3(0, 1, 0));
}

TEST(GridRay, NegativeCoordinates) {
    Grid3 grid;
    grid.add(-1, 0, 0);
    const auto hit = engine::raycast_grid(engine::Ray{{-0.5f, -2.5f, 0.5f}, {0.0f, 1.0f, 0.0f}}, 10.0f, grid);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->cell, glm::ivec3(-1, 0, 0));
    EXPECT_EQ(hit->normal, glm::ivec3(0, -1, 0));
    EXPECT_NEAR(hit->t, 2.5f, kEpsilon);
}

// --- sweep_aabb (3D) ---

TEST(GridSweep, FreeMoveNoHit) {
    const engine::render::Aabb box = player_box(0.2f, 1.5f);
    const engine::GridSweep r = engine::sweep_aabb(box, {0.3f, 0.2f, -0.1f}, Grid3{});
    EXPECT_EQ(r.hit, glm::ivec3(0));
    expect_vec_near(r.moved, {0.3f, 0.2f, -0.1f});
    expect_vec_near(r.box.min, box.min + glm::vec3{0.3f, 0.2f, -0.1f});
    expect_vec_near(r.box.max, box.max + glm::vec3{0.3f, 0.2f, -0.1f});
    EXPECT_FALSE(r.grounded());
}

TEST(GridSweep, LandsOnFloorGrounded) {
    const engine::GridSweep r = engine::sweep_aabb(player_box(0.2f, 1.5f), {0.0f, -1.0f, 0.0f}, floor_grid());
    EXPECT_EQ(r.hit, glm::ivec3(0, -1, 0));
    EXPECT_TRUE(r.grounded());
    EXPECT_EQ(r.box.min.y, 1.0f);
    EXPECT_NEAR(r.box.max.y, 2.8f, kEpsilon);
    EXPECT_NEAR(r.moved.y, -0.5f, kEpsilon);
}

TEST(GridSweep, RestingOnFloorStaysPutEveryFrame) {
    const Grid3 grid = floor_grid();
    engine::render::Aabb box = player_box(0.2f, 1.0f);
    for (int i = 0; i < 1000; ++i) {
        // Gravity plus a slow walk across the floor's cell seams.
        const engine::GridSweep r = engine::sweep_aabb(box, {0.003f, -0.01f, 0.0f}, grid);
        ASSERT_TRUE(r.grounded()) << i;
        ASSERT_EQ(r.box.min.y, 1.0f) << i;
        ASSERT_EQ(r.hit.x, 0) << i;
        box = r.box;
    }
    EXPECT_NEAR(box.min.x, 3.2f, 1e-3f);
}

TEST(GridSweep, SunkWithinSkinIsPushedUp) {
    const engine::GridSweep r =
            engine::sweep_aabb(player_box(0.2f, 1.0f - 5e-4f), {0.0f, -0.01f, 0.0f}, floor_grid());
    EXPECT_TRUE(r.grounded());
    EXPECT_EQ(r.box.min.y, 1.0f);
}

TEST(GridSweep, CeilingHit) {
    Grid3 grid;
    grid.add(0, 5, 0);
    const engine::GridSweep r = engine::sweep_aabb(player_box(0.2f, 1.5f), {0.0f, 3.0f, 0.0f}, grid);
    EXPECT_EQ(r.hit, glm::ivec3(0, 1, 0));
    EXPECT_EQ(r.box.max.y, 5.0f);
    EXPECT_NEAR(r.box.min.y, 3.2f, kEpsilon);
    EXPECT_FALSE(r.grounded());
}

TEST(GridSweep, SlidesAlongWall) {
    Grid3 grid = floor_grid();
    add_wall(grid, 2, 4);
    const engine::GridSweep r = engine::sweep_aabb(player_box(0.2f, 1.0f), {2.0f, -0.1f, 1.5f}, grid);
    EXPECT_EQ(r.hit, glm::ivec3(1, -1, 0));
    EXPECT_EQ(r.box.max.x, 2.0f);
    EXPECT_NEAR(r.box.min.x, 1.4f, kEpsilon);
    EXPECT_NEAR(r.box.min.z, 1.7f, kEpsilon);
    EXPECT_NEAR(r.moved.z, 1.5f, kEpsilon);
    EXPECT_EQ(r.box.min.y, 1.0f);
}

TEST(GridSweep, MovesExactlyAlongWallEdge) {
    Grid3 grid = floor_grid();
    add_wall(grid, 2, 4);
    const engine::render::Aabb flush{{1.4f, 1.0f, 0.2f}, {2.0f, 2.8f, 0.8f}};

    const engine::GridSweep along = engine::sweep_aabb(flush, {0.0f, 0.0f, 3.0f}, grid);
    EXPECT_EQ(along.hit, glm::ivec3(0));
    EXPECT_NEAR(along.moved.z, 3.0f, kEpsilon);
    EXPECT_EQ(along.box.max.x, 2.0f);

    const engine::GridSweep up = engine::sweep_aabb(flush, {0.0f, 2.0f, 0.0f}, grid);
    EXPECT_EQ(up.hit, glm::ivec3(0));
    EXPECT_NEAR(up.moved.y, 2.0f, kEpsilon);

    // Flush on the floor, across its cell seams.
    const engine::GridSweep across = engine::sweep_aabb(player_box(-5.2f, 1.0f), {5.0f, 0.0f, 0.0f}, floor_grid());
    EXPECT_EQ(across.hit, glm::ivec3(0));
    EXPECT_NEAR(across.moved.x, 5.0f, kEpsilon);
    EXPECT_EQ(across.box.min.y, 1.0f);
}

TEST(GridSweep, MovesAlongCornerEdge) {
    Grid3 grid = floor_grid();
    add_wall(grid, 2, 4);
    const engine::render::Aabb corner{{1.4f, 1.0f, 0.2f}, {2.0f, 2.8f, 0.8f}};

    const engine::GridSweep along = engine::sweep_aabb(corner, {0.0f, -0.01f, 2.0f}, grid);
    EXPECT_EQ(along.hit, glm::ivec3(0, -1, 0));
    EXPECT_NEAR(along.moved.z, 2.0f, kEpsilon);
    EXPECT_EQ(along.box.min.y, 1.0f);
    EXPECT_EQ(along.box.max.x, 2.0f);

    const engine::GridSweep pushing = engine::sweep_aabb(corner, {0.5f, -0.01f, 2.0f}, grid);
    EXPECT_EQ(pushing.hit, glm::ivec3(1, -1, 0));
    EXPECT_NEAR(pushing.moved.z, 2.0f, kEpsilon);
    EXPECT_EQ(pushing.box.max.x, 2.0f);
}

TEST(GridSweep, StepBlocksWithoutStepUp) {
    Grid3 grid = floor_grid();
    grid.add(2, 1, 0);
    const engine::GridSweep r = engine::sweep_aabb(player_box(0.2f, 1.0f), {3.0f, -0.01f, 0.0f}, grid);
    EXPECT_EQ(r.hit, glm::ivec3(1, -1, 0));
    EXPECT_EQ(r.box.max.x, 2.0f);
    EXPECT_EQ(r.box.min.y, 1.0f);
}

TEST(GridSweep, HighSpeedDoesNotTunnel) {
    {
        Grid3 grid;
        grid.add(0, 0, 0);
        const engine::GridSweep r = engine::sweep_aabb(player_box(0.2f, 500.0f), {0.0f, -1000.0f, 0.0f}, grid);
        EXPECT_TRUE(r.grounded());
        EXPECT_EQ(r.box.min.y, 1.0f);
    }
    {
        Grid3 grid;
        grid.add(2, 1, 0);
        grid.add(2, 2, 0);
        const engine::GridSweep r = engine::sweep_aabb(player_box(0.2f, 1.0f), {500.0f, 0.0f, 0.0f}, grid);
        EXPECT_EQ(r.hit, glm::ivec3(1, 0, 0));
        EXPECT_EQ(r.box.max.x, 2.0f);
    }
    {
        Grid3 grid = floor_grid();
        for (int y = 1; y <= 600; ++y) {
            grid.add(2, y, 0);
        }
        const engine::GridSweep r = engine::sweep_aabb(player_box(0.2f, 500.0f), {1000.0f, -1000.0f, 0.0f}, grid);
        EXPECT_EQ(r.hit, glm::ivec3(1, -1, 0));
        EXPECT_EQ(r.box.max.x, 2.0f);
        EXPECT_EQ(r.box.min.y, 1.0f);
    }
}

TEST(GridSweep, DiagonalFallKeepsPath) {
    // Axis by axis in one step would fall past the block first and slide under it; three sub-steps meet it.
    Grid3 grid;
    grid.add(2, 1, 0);
    const engine::render::Aabb box{{0.2f, 3.2f, 0.2f}, {0.8f, 3.8f, 0.8f}};
    const engine::GridSweep r = engine::sweep_aabb(box, {3.0f, -3.0f, 0.0f}, grid);
    EXPECT_EQ(r.hit, glm::ivec3(1, 0, 0));
    EXPECT_EQ(r.box.max.x, 2.0f);
    EXPECT_NEAR(r.box.min.y, 0.2f, kEpsilon);
}

TEST(GridSweep, InsideSolidIsNotStuck) {
    Grid3 grid;
    grid.add(0, 1, 0);
    const engine::render::Aabb box{{0.2f, 1.2f, 0.2f}, {0.8f, 1.8f, 0.8f}};
    const engine::GridSweep up = engine::sweep_aabb(box, {0.0f, 1.0f, 0.0f}, grid);
    EXPECT_EQ(up.hit, glm::ivec3(0));
    EXPECT_NEAR(up.box.min.y, 2.2f, kEpsilon);
    const engine::GridSweep side = engine::sweep_aabb(box, {2.0f, 0.0f, 0.0f}, grid);
    EXPECT_EQ(side.hit, glm::ivec3(0));
    EXPECT_NEAR(side.box.min.x, 2.2f, kEpsilon);
}

TEST(GridSweep, ExactLandingCountsAsHit) {
    const engine::GridSweep r = engine::sweep_aabb(player_box(0.2f, 2.0f), {0.0f, -1.0f, 0.0f}, floor_grid());
    EXPECT_TRUE(r.grounded());
    EXPECT_EQ(r.box.min.y, 1.0f);
}

TEST(GridSweep, ZeroDeltaReportsNoHit) {
    const engine::GridSweep r = engine::sweep_aabb(player_box(0.2f, 1.0f), glm::vec3{0.0f}, floor_grid());
    EXPECT_EQ(r.hit, glm::ivec3(0));
    EXPECT_FALSE(r.grounded());
    EXPECT_EQ(r.box.min.y, 1.0f);
}

TEST(GridSweep, ReversedBoxAndNonFiniteDelta) {
    const engine::render::Aabb reversed{{0.8f, 2.8f, 0.8f}, {0.2f, 1.5f, 0.2f}};
    const engine::GridSweep r = engine::sweep_aabb(reversed, {1.0f, 0.0f, 0.0f}, Grid3{});
    expect_vec_near(r.box.min, {1.2f, 1.5f, 0.2f});
    expect_vec_near(r.box.max, {1.8f, 2.8f, 0.8f});
    expect_vec_near(r.moved, {1.0f, 0.0f, 0.0f});

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const engine::GridSweep bad = engine::sweep_aabb(reversed, {nan, -1.0f, 0.0f}, floor_grid());
    EXPECT_EQ(bad.hit, glm::ivec3(0));
    EXPECT_EQ(bad.box.min, glm::vec3(0.2f, 1.5f, 0.2f));
    EXPECT_EQ(bad.moved, glm::vec3(0.0f));
}

// --- 2D ---

TEST(GridRay2D, AxesDiagonalsInsideAndMaxDistance) {
    const glm::vec2 origin{0.5f, 0.5f};
    for (const glm::ivec2 axis : {glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1}, glm::ivec2{0, -1}}) {
        Grid2 grid;
        grid.add(axis.x * 3, axis.y * 3);
        const auto hit = engine::raycast_grid(origin, glm::vec2{axis}, 10.0f, grid);
        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->cell, axis * 3);
        EXPECT_EQ(hit->normal, -axis);
        EXPECT_NEAR(hit->t, 2.5f, kEpsilon);
        EXPECT_FALSE(engine::raycast_grid(origin, glm::vec2{axis}, 2.4f, grid));
    }

    Grid2 diagonal;
    diagonal.add(3, 3);
    const auto hit = engine::raycast_grid(origin, {1.0f, 1.0f}, 10.0f, diagonal);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->cell, glm::ivec2(3, 3));
    EXPECT_EQ(hit->normal, glm::ivec2(-1, 0));
    EXPECT_NEAR(hit->t, 2.5f, kEpsilon);

    Grid2 inside;
    inside.add(0, 0);
    const auto start = engine::raycast_grid(origin, {1.0f, 0.0f}, 10.0f, inside);
    ASSERT_TRUE(start);
    EXPECT_EQ(start->t, 0.0f);
    EXPECT_EQ(start->normal, glm::ivec2(0));
}

TEST(GridRay2D, ExactCorner) {
    for (const glm::ivec2 side : {glm::ivec2{1, 0}, glm::ivec2{0, 1}}) {
        Grid2 grid;
        grid.add(side.x, side.y);
        const auto hit = engine::raycast_grid({0.5f, 0.5f}, {1.0f, 1.0f}, 5.0f, grid);
        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->cell, side);
        EXPECT_EQ(hit->normal, -side);
        EXPECT_NEAR(hit->t, 0.5f, kEpsilon);
    }
}

TEST(GridSweep2D, LandsSlidesHitsCeilingAndDoesNotTunnel) {
    Grid2 grid;
    for (int x = -10; x <= 10; ++x) {
        grid.add(x, 0);
        grid.add(x, 6);
    }
    for (int y = 1; y <= 5; ++y) {
        grid.add(3, y);
    }

    const engine::GridSweep2D land = engine::sweep_aabb({0.2f, 2.0f}, {0.8f, 3.0f}, {0.0f, -5.0f}, grid);
    EXPECT_TRUE(land.grounded());
    EXPECT_EQ(land.min.y, 1.0f);
    EXPECT_EQ(land.hit, glm::ivec2(0, -1));

    const engine::GridSweep2D slide = engine::sweep_aabb({0.2f, 1.0f}, {0.8f, 2.0f}, {5.0f, -0.1f}, grid);
    EXPECT_EQ(slide.hit, glm::ivec2(1, -1));
    EXPECT_EQ(slide.max.x, 3.0f);
    EXPECT_EQ(slide.min.y, 1.0f);

    const engine::GridSweep2D ceiling = engine::sweep_aabb({0.2f, 1.0f}, {0.8f, 2.0f}, {0.0f, 10.0f}, grid);
    EXPECT_EQ(ceiling.hit, glm::ivec2(0, 1));
    EXPECT_EQ(ceiling.max.y, 6.0f);

    const engine::GridSweep2D fast = engine::sweep_aabb({0.2f, 2.0f}, {0.8f, 3.0f}, {0.0f, -1000.0f}, grid);
    EXPECT_TRUE(fast.grounded());
    EXPECT_EQ(fast.min.y, 1.0f);
}

TEST(GridSweep2D, TilemapPredicate) {
    // An 8 x 4 map whose bottom row (row 3) is ground. Row r spans grid y in [-(r + 1), -r).
    auto asset = std::make_shared<engine::render::TilemapAsset>();
    asset->desc.width = 8;
    asset->desc.height = 4;
    std::vector<std::uint16_t> tiles(32, 0);
    for (int x = 0; x < 8; ++x) {
        tiles[static_cast<std::size_t>(3 * 8 + x)] = 1;
    }
    asset->desc.layers.push_back(engine::render::TilemapLayerDesc{"ground", tiles});
    const engine::render::Tilemap map = engine::render::Tilemap::from_asset(asset);

    const auto solid = [&](glm::ivec2 c) { return map.tile(0, c.x, -c.y - 1) != 0; };
    const engine::GridSweep2D r = engine::sweep_aabb({2.2f, -1.5f}, {2.8f, -0.5f}, {0.0f, -5.0f}, solid);
    EXPECT_TRUE(r.grounded());
    EXPECT_EQ(r.min.y, -3.0f);
}

// --- CellPredicate ---

TEST(CellPredicate, WrapsCallablesWithoutCopying) {
    struct Probe {
        int* copies;
        int* calls;
        Probe(int* c, int* n) : copies(c), calls(n) {}
        Probe(const Probe& other) : copies(other.copies), calls(other.calls) {
            ++*copies;
        }
        bool operator()(glm::ivec3 c) const {
            ++*calls;
            return c.x == 2;
        }
    };
    int copies = 0;
    int calls = 0;
    const Probe probe{&copies, &calls};
    const auto hit = engine::raycast_grid(engine::Ray{{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}}, 10.0f, probe);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->cell.x, 2);
    EXPECT_EQ(copies, 0);
    EXPECT_EQ(calls, 3);

    bool (*fn)(glm::ivec3) = &never_solid;
    EXPECT_FALSE(engine::raycast_grid(engine::Ray{{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}}, 10.0f, fn));
}
