#include <gtest/gtest.h>

#include <engine/ecs/ray.h>

namespace {

constexpr float kEpsilon = 1e-4f;

engine::Ray ray_along(glm::vec3 origin, glm::vec3 direction) {
    return engine::Ray{origin, direction};
}

}

TEST(Ray, PlaneHitsInFrontAndKeepsALineBehind) {
    const engine::Ray ray = ray_along({0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, -1.0f});
    const std::optional<engine::RayHit> hit = engine::ray_plane(ray, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f});
    ASSERT_TRUE(hit);
    EXPECT_NEAR(hit->t, 4.0f, kEpsilon);
    EXPECT_NEAR(hit->point.z, 0.0f, kEpsilon);
    EXPECT_NEAR(hit->normal.z, 1.0f, kEpsilon);

    const engine::Ray away = ray_along({0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 1.0f});
    const std::optional<engine::RayHit> behind = engine::line_plane(away, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(behind);
    EXPECT_LT(behind->t, 0.0f);
    EXPECT_FALSE(engine::ray_plane(away, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}));
    EXPECT_FALSE(engine::ray_plane(ray_along({0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}), {0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}));
    EXPECT_FALSE(engine::line_plane(ray, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}));
}

TEST(Ray, SphereHitsTheNearSurfaceAndTheInside) {
    const engine::Ray ray = ray_along({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f});
    const std::optional<engine::RayHit> hit = engine::ray_sphere(ray, {0.0f, 0.0f, 0.0f}, 1.0f);
    ASSERT_TRUE(hit);
    EXPECT_NEAR(hit->t, 4.0f, kEpsilon);
    EXPECT_NEAR(hit->point.z, 1.0f, kEpsilon);
    EXPECT_NEAR(hit->normal.z, 1.0f, kEpsilon);

    EXPECT_FALSE(engine::ray_sphere(ray, {3.0f, 0.0f, 0.0f}, 0.5f));
    EXPECT_FALSE(engine::ray_sphere(ray_along({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, -1.0f}), {0.0f, 0.0f, 0.0f}, 1.0f));
    EXPECT_FALSE(engine::ray_sphere(ray, {0.0f, 0.0f, 0.0f}, -1.0f));

    const std::optional<engine::RayHit> inside =
            engine::ray_sphere(ray_along({0.2f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}), {0.0f, 0.0f, 0.0f}, 1.0f);
    ASSERT_TRUE(inside);
    EXPECT_NEAR(inside->t, 0.0f, kEpsilon);
    EXPECT_NEAR(inside->point.x, 0.2f, kEpsilon);
}

TEST(Ray, AabbHitsTheEntryFaceAndSwapsReversedBounds) {
    const engine::Ray ray = ray_along({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f});
    const std::optional<engine::RayHit> hit = engine::ray_aabb(ray, {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f});
    ASSERT_TRUE(hit);
    EXPECT_NEAR(hit->t, 4.0f, kEpsilon);
    EXPECT_NEAR(hit->point.z, 1.0f, kEpsilon);
    EXPECT_NEAR(hit->normal.z, 1.0f, kEpsilon);

    const std::optional<engine::RayHit> swapped = engine::ray_aabb(ray, {1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f});
    ASSERT_TRUE(swapped);
    EXPECT_NEAR(swapped->t, hit->t, kEpsilon);

    EXPECT_FALSE(engine::ray_aabb(ray_along({3.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}), {-1.0f, -1.0f, -1.0f},
            {1.0f, 1.0f, 1.0f}));

    const std::optional<engine::RayHit> inside = engine::ray_aabb(
            ray_along({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}), {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f});
    ASSERT_TRUE(inside);
    EXPECT_NEAR(inside->t, 0.0f, kEpsilon);
    EXPECT_NEAR(inside->normal.x, -1.0f, kEpsilon);
}

TEST(Ray, TriangleAndQuadHitEitherSide) {
    const glm::vec3 a{-1.0f, -1.0f, 0.0f};
    const glm::vec3 b{1.0f, -1.0f, 0.0f};
    const glm::vec3 c{1.0f, 1.0f, 0.0f};
    const glm::vec3 d{-1.0f, 1.0f, 0.0f};
    const engine::Ray front = ray_along({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, -1.0f});
    const std::optional<engine::RayHit> quad = engine::ray_quad(front, a, b, c, d);
    ASSERT_TRUE(quad);
    EXPECT_NEAR(quad->t, 3.0f, kEpsilon);
    EXPECT_NEAR(quad->normal.z, 1.0f, kEpsilon);

    const engine::Ray back = ray_along({0.0f, 0.0f, -3.0f}, {0.0f, 0.0f, 1.0f});
    const std::optional<engine::RayHit> behind = engine::ray_triangle(back, a, b, c);
    ASSERT_TRUE(behind);
    EXPECT_NEAR(behind->t, 3.0f, kEpsilon);
    EXPECT_NEAR(behind->normal.z, -1.0f, kEpsilon);

    EXPECT_FALSE(engine::ray_triangle(ray_along({3.0f, 3.0f, 3.0f}, {0.0f, 0.0f, -1.0f}), a, b, c));
    EXPECT_FALSE(engine::ray_quad(ray_along({0.0f, 0.0f, -3.0f}, {0.0f, 0.0f, -1.0f}), a, b, c, d));
}

TEST(Ray, SegmentHitsWithinRadiusAlongTheNormalizedRay) {
    const std::optional<engine::RayHit> hit =
            engine::ray_segment(ray_along({1.0f, 0.05f, 0.0f}, {0.0f, -2.0f, 0.0f}), {0.0f, 0.0f, 0.0f},
                    {2.0f, 0.0f, 0.0f}, 2.0f, 0.1f);
    ASSERT_TRUE(hit);
    EXPECT_NEAR(hit->t, 0.05f, kEpsilon);
    EXPECT_NEAR(hit->point.x, 1.0f, kEpsilon);
    EXPECT_NEAR(hit->point.y, 0.0f, kEpsilon);

    EXPECT_FALSE(engine::ray_segment(ray_along({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}), {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f}, 2.0f, 0.1f));
    EXPECT_FALSE(engine::ray_segment(ray_along({1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}), {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f}, 2.0f, -0.1f));
}
