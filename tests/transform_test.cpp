#include <gtest/gtest.h>

#include <engine/ecs/transform.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

void expect_near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(a[c][r], b[c][r], eps) << "column " << c << " row " << r;
        }
    }
}

void expect_near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

// The model matrix from before rotation was a quaternion: T * Rx * Ry * Rz * S from Euler radians.
glm::mat4 euler_trs(glm::vec3 position, glm::vec3 euler, glm::vec3 scale) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    model = glm::rotate(model, euler.x, glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, euler.y, glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, euler.z, glm::vec3{0.0f, 0.0f, 1.0f});
    return glm::scale(model, scale);
}

}

TEST(Transform, DefaultIsIdentity) {
    const engine::Transform transform;
    expect_near(engine::model_matrix(transform), glm::mat4(1.0f));
    EXPECT_FLOAT_EQ(engine::angle_z(transform.rotation), 0.0f);
}

TEST(Transform, ZOnlyRotationMatchesEulerModelMatrix) {
    const glm::vec3 position{3.0f, -2.0f, 0.5f};
    const glm::vec3 scale{2.0f, 0.5f, 1.0f};
    for (const float angle : {0.0f, 0.3f, -1.2f, glm::half_pi<float>(), 3.0f}) {
        const engine::Transform transform{.position = position, .rotation = engine::rotation_z(angle), .scale = scale};
        expect_near(engine::model_matrix(transform), euler_trs(position, {0.0f, 0.0f, angle}, scale));
    }
}

TEST(Transform, EulerXyzMatchesEulerModelMatrix) {
    const glm::vec3 position{1.0f, 2.0f, 3.0f};
    const glm::vec3 euler{0.1f, 0.2f, 0.3f};
    const glm::vec3 scale{2.0f, 3.0f, 4.0f};
    const engine::Transform transform{.position = position, .rotation = engine::euler_xyz(euler), .scale = scale};
    expect_near(engine::model_matrix(transform), euler_trs(position, euler, scale), 1e-4f);
}

TEST(Transform, RotationZRoundTripsThroughAngleZ) {
    for (const float angle : {0.0f, 0.25f, -0.25f, 1.5f, -2.5f, 3.1f}) {
        EXPECT_NEAR(engine::angle_z(engine::rotation_z(angle)), angle, 1e-5f) << angle;
        EXPECT_NEAR(engine::angle_z(engine::euler_xyz({0.0f, 0.0f, angle})), angle, 1e-5f) << angle;
    }
    // Past pi it wraps into (-pi, pi].
    EXPECT_NEAR(engine::angle_z(engine::rotation_z(glm::pi<float>() + 0.5f)), -glm::pi<float>() + 0.5f, 1e-5f);
}

TEST(Transform, RotationZTurnsCounterClockwise) {
    const glm::vec3 turned = engine::rotation_z(glm::half_pi<float>()) * glm::vec3{1.0f, 0.0f, 0.0f};
    expect_near(turned, {0.0f, 1.0f, 0.0f});
}

TEST(Transform, AngleZOfThreeDimensionalRotationIsHeadingOfXAxis) {
    // A tilt about X before the Z turn leaves the X axis alone, so the 2D heading is still the Z turn.
    EXPECT_NEAR(engine::angle_z(engine::rotation_z(0.4f) * engine::euler_xyz({0.7f, 0.0f, 0.0f})), 0.4f, 1e-5f);
    // Turning X onto -Z has no heading in the XY plane.
    EXPECT_FLOAT_EQ(engine::angle_z(engine::euler_xyz({0.0f, glm::half_pi<float>(), 0.0f})), 0.0f);
}
