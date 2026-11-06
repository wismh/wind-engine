#include <gtest/gtest.h>

#include <engine/ecs/physics_3d.h>
#include <engine/ecs/transform.h>

#include "ecs/contact_3d.h"
#include "ecs/inertia_3d.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace {

engine::ContactShape3D sphere(glm::vec3 center, float radius) {
    engine::ContactShape3D shape;
    shape.kind = engine::ContactShapeKind3D::Sphere;
    shape.center = center;
    shape.radius = radius;
    return shape;
}

engine::ContactShape3D box(glm::vec3 center, glm::vec3 full_size, glm::quat rotation = glm::quat{1.f, 0.f, 0.f, 0.f}) {
    engine::ContactShape3D shape;
    shape.kind = engine::ContactShapeKind3D::Box;
    shape.center = center;
    shape.rotation = rotation;
    shape.half_extents = full_size * 0.5f;
    return shape;
}

engine::ContactShape3D capsule(glm::vec3 center, float radius, float height,
                               glm::quat rotation = glm::quat{1.f, 0.f, 0.f, 0.f}) {
    engine::ContactShape3D shape;
    shape.kind = engine::ContactShapeKind3D::Capsule;
    shape.center = center;
    shape.rotation = rotation;
    shape.radius = radius;
    shape.height = height;
    return shape;
}

}

TEST(Physics3DContact, CoincidentSpheresUsePositiveX) {
    const engine::ContactManifold3D hit =
            engine::make_contact_3d(sphere(glm::vec3{0.f}, 0.5f), sphere(glm::vec3{0.f}, 0.25f));
    ASSERT_EQ(hit.count, 1);
    EXPECT_EQ(hit.normal, (glm::vec3{1.f, 0.f, 0.f}));
    EXPECT_NEAR(hit.points[0].penetration, 0.75f, 1e-6f);
    EXPECT_TRUE(std::isfinite(hit.points[0].point.x));
}

TEST(Physics3DContact, NegativeSizeAndRadiusMiss) {
    engine::ContactShape3D bad_box = box(glm::vec3{0.f}, glm::vec3{1.f});
    bad_box.half_extents.y = -1.f;
    EXPECT_EQ(engine::make_contact_3d(bad_box, sphere(glm::vec3{0.f}, 0.5f)).count, 0);
    EXPECT_EQ(engine::make_contact_3d(sphere(glm::vec3{0.f}, -0.5f), sphere(glm::vec3{0.f}, 0.5f)).count, 0);
    EXPECT_EQ(engine::make_contact_3d(capsule(glm::vec3{0.f}, 0.5f, -1.f), sphere(glm::vec3{0.f}, 0.5f)).count, 0);
}

TEST(Physics3DContact, IdentitySphereBoxMatchesAabb) {
    const engine::ContactManifold3D hit =
            engine::make_contact_3d(box(glm::vec3{0.f}, glm::vec3{2.f}), sphere(glm::vec3{0.f, 1.2f, 0.f}, 0.5f));
    ASSERT_EQ(hit.count, 1);
    EXPECT_NEAR(hit.points[0].penetration, 0.3f, 1e-5f);
    EXPECT_NEAR(hit.normal.x, 0.f, 1e-5f);
    EXPECT_NEAR(hit.normal.y, 1.f, 1e-5f);
    EXPECT_NEAR(hit.normal.z, 0.f, 1e-5f);
    EXPECT_NEAR(hit.points[0].point.x, 0.f, 1e-5f);
    EXPECT_NEAR(hit.points[0].point.y, 0.85f, 1e-5f);
    EXPECT_NEAR(hit.points[0].point.z, 0.f, 1e-5f);
}

TEST(Physics3DContact, RotatedSphereBoxUsesTheObb) {
    // Local (0.5, 0, 0) is closer to the +X face than to Y. World position is that point rotated 45 degrees,
    // so the outward normal must follow the box, not stay on world +X.
    const glm::quat yaw = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3{0.f, 0.f, 1.f});
    const glm::vec3 center = yaw * glm::vec3{0.5f, 0.f, 0.f};
    const engine::ContactManifold3D hit =
            engine::make_contact_3d(box(glm::vec3{0.f}, glm::vec3{2.f}, yaw), sphere(center, 0.5f));
    ASSERT_EQ(hit.count, 1);
    EXPECT_NEAR(hit.points[0].penetration, 1.f, 1e-5f);
    EXPECT_NEAR(hit.normal.x, std::cos(glm::quarter_pi<float>()), 1e-5f);
    EXPECT_NEAR(hit.normal.y, std::sin(glm::quarter_pi<float>()), 1e-5f);
    EXPECT_NEAR(hit.normal.z, 0.f, 1e-5f);
}

TEST(Physics3DContact, YawedBoxSwapsXExtentAgainstASphere) {
    const glm::quat yaw = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.f, 1.f, 0.f});
    const engine::ContactShape3D ball = sphere(glm::vec3{1.6f, 0.f, 0.f}, 0.5f);
    const engine::ContactManifold3D identity =
            engine::make_contact_3d(box(glm::vec3{0.f}, glm::vec3{4.f, 2.f, 1.f}), ball);
    const engine::ContactManifold3D turned =
            engine::make_contact_3d(box(glm::vec3{0.f}, glm::vec3{4.f, 2.f, 1.f}, yaw), ball);
    ASSERT_EQ(identity.count, 1);
    EXPECT_NEAR(identity.points[0].penetration, 0.9f, 1e-5f);
    EXPECT_EQ(turned.count, 0);
}

TEST(Physics3DContact, SphereCapsuleAndCapsuleCapsule) {
    const engine::ContactManifold3D sphere_hit = engine::make_contact_3d(
            capsule(glm::vec3{0.f}, 0.5f, 1.f), sphere(glm::vec3{0.8f, 0.f, 0.f}, 0.5f));
    ASSERT_EQ(sphere_hit.count, 1);
    EXPECT_NEAR(sphere_hit.points[0].penetration, 0.2f, 1e-5f);
    EXPECT_NEAR(sphere_hit.normal.x, 1.f, 1e-5f);
    EXPECT_NEAR(sphere_hit.points[0].point.x, 0.4f, 1e-5f);

    const engine::ContactManifold3D pair = engine::make_contact_3d(capsule(glm::vec3{0.f}, 0.5f, 1.f),
                                                                   capsule(glm::vec3{0.5f, 0.f, 0.f}, 0.5f, 1.f));
    ASSERT_EQ(pair.count, 1);
    EXPECT_NEAR(pair.points[0].penetration, 0.5f, 1e-5f);
    EXPECT_NEAR(pair.normal.x, 1.f, 1e-5f);

    const engine::ContactManifold3D same = engine::make_contact_3d(capsule(glm::vec3{1.f, 2.f, 3.f}, 0.4f, 2.f),
                                                                   capsule(glm::vec3{1.f, 2.f, 3.f}, 0.4f, 2.f));
    ASSERT_EQ(same.count, 1);
    EXPECT_EQ(same.normal, (glm::vec3{1.f, 0.f, 0.f}));
    EXPECT_NEAR(same.points[0].penetration, 0.8f, 1e-5f);
}

TEST(Physics3DContact, IdentityBoxBoxUsesOverlapCenter) {
    const engine::ContactManifold3D hit = engine::make_contact_3d(
            box(glm::vec3{0.f}, glm::vec3{2.f}), box(glm::vec3{1.5f, 0.f, 0.f}, glm::vec3{2.f}));
    ASSERT_EQ(hit.count, 1);
    EXPECT_NEAR(hit.points[0].penetration, 0.5f, 1e-5f);
    EXPECT_EQ(hit.normal, (glm::vec3{1.f, 0.f, 0.f}));
    EXPECT_NEAR(hit.points[0].point.x, 0.75f, 1e-5f);
    EXPECT_NEAR(hit.points[0].point.y, 0.f, 1e-5f);
    EXPECT_NEAR(hit.points[0].point.z, 0.f, 1e-5f);
}

TEST(Physics3DContact, RotatedBoxBoxUsesSat) {
    const glm::quat yaw = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3{0.f, 0.f, 1.f});
    const engine::ContactManifold3D overlap = engine::make_contact_3d(
            box(glm::vec3{0.f}, glm::vec3{2.f}), box(glm::vec3{2.f, 0.f, 0.f}, glm::vec3{2.f}, yaw));
    ASSERT_EQ(overlap.count, 1);
    EXPECT_GT(overlap.points[0].penetration, 0.f);
    EXPECT_NEAR(glm::length(overlap.normal), 1.f, 1e-5f);
    EXPECT_GT(glm::dot(overlap.normal, glm::vec3{1.f, 0.f, 0.f}), 0.f);

    const engine::ContactManifold3D separated = engine::make_contact_3d(
            box(glm::vec3{0.f}, glm::vec3{2.f}), box(glm::vec3{5.f, 0.f, 0.f}, glm::vec3{2.f}, yaw));
    EXPECT_EQ(separated.count, 0);
}

TEST(Physics3DContact, CapsuleBoxOutsideAndInside) {
    const engine::ContactManifold3D outside = engine::make_contact_3d(
            box(glm::vec3{0.f}, glm::vec3{2.f}), capsule(glm::vec3{1.2f, 0.f, 0.f}, 0.5f, 1.f));
    ASSERT_EQ(outside.count, 1);
    EXPECT_NEAR(outside.points[0].penetration, 0.3f, 1e-4f);
    EXPECT_NEAR(outside.normal.x, 1.f, 1e-4f);

    const engine::ContactManifold3D inside =
            engine::make_contact_3d(box(glm::vec3{0.f}, glm::vec3{2.f}), capsule(glm::vec3{0.f}, 0.5f, 1.f));
    ASSERT_EQ(inside.count, 1);
    EXPECT_NEAR(inside.points[0].penetration, 1.5f, 1e-4f);
    EXPECT_NEAR(inside.normal.x, 1.f, 1e-4f);

    const engine::ContactManifold3D miss =
            engine::make_contact_3d(box(glm::vec3{0.f}, glm::vec3{2.f}), capsule(glm::vec3{3.f, 0.f, 0.f}, 0.5f, 1.f));
    EXPECT_EQ(miss.count, 0);
}

TEST(Physics3DContact, FlatCapsuleMatchesSolidSphereInertia) {
    const glm::vec3 sphere = engine::inertia_3d_sphere(2.f, 0.4f);
    const glm::vec3 flat = engine::inertia_3d_capsule(2.f, 0.4f, 0.f);
    EXPECT_NEAR(flat.x, sphere.x, 1e-5f);
    EXPECT_NEAR(flat.y, sphere.y, 1e-5f);
    EXPECT_NEAR(flat.z, sphere.z, 1e-5f);
    EXPECT_NEAR(sphere.x, 0.4f * 2.f * 0.16f, 1e-6f);
}
