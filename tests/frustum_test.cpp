#include <gtest/gtest.h>

#include <engine/ecs/camera.h>
#include <engine/ecs/transform.h>
#include <engine/render/aabb.h>
#include <engine/render/frustum.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace {

using engine::render::Aabb;
using engine::render::Frustum;
using engine::render::frustum_intersects;
using engine::render::make_frustum;

Aabb box_at(glm::vec3 center, glm::vec3 half) {
    return Aabb{.min = center - half, .max = center + half};
}

// x and y in [-5, 5], z in [-1, 1]: the default 2D camera at the origin with ortho_size 5 on a square window.
Frustum ortho_frustum() {
    return make_frustum(glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, -1.0f, 1.0f));
}

// At the origin looking down -Z, 90 degrees vertical, square, near 1, far 100.
Frustum perspective_frustum() {
    return make_frustum(glm::perspective(glm::half_pi<float>(), 1.0f, 1.0f, 100.0f));
}

void expect_box_near(const Aabb& actual, const Aabb& expected) {
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_NEAR(actual.min[axis], expected.min[axis], 1e-5f) << "min axis " << axis;
        EXPECT_NEAR(actual.max[axis], expected.max[axis], 1e-5f) << "max axis " << axis;
    }
}

}

TEST(Frustum, OrthographicBoxInsideIsVisible) {
    EXPECT_TRUE(frustum_intersects(ortho_frustum(), box_at({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f})));
    EXPECT_TRUE(frustum_intersects(ortho_frustum(), box_at({3.0f, -3.0f, 0.5f}, {0.5f, 0.5f, 0.1f})));
}

TEST(Frustum, OrthographicBoxOutsideEachPlaneIsCulled) {
    const Frustum frustum = ortho_frustum();
    const glm::vec3 half{0.5f, 0.5f, 0.1f};
    EXPECT_FALSE(frustum_intersects(frustum, box_at({-7.0f, 0.0f, 0.0f}, half))) << "left";
    EXPECT_FALSE(frustum_intersects(frustum, box_at({7.0f, 0.0f, 0.0f}, half))) << "right";
    EXPECT_FALSE(frustum_intersects(frustum, box_at({0.0f, -7.0f, 0.0f}, half))) << "bottom";
    EXPECT_FALSE(frustum_intersects(frustum, box_at({0.0f, 7.0f, 0.0f}, half))) << "top";
    // The view looks down -Z: near is the plane z = 1 and far the plane z = -1.
    EXPECT_FALSE(frustum_intersects(frustum, box_at({0.0f, 0.0f, 3.0f}, half))) << "near";
    EXPECT_FALSE(frustum_intersects(frustum, box_at({0.0f, 0.0f, -3.0f}, half))) << "far";
}

TEST(Frustum, StraddlingBoxIsVisible) {
    const Frustum frustum = ortho_frustum();
    EXPECT_TRUE(frustum_intersects(frustum, box_at({5.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f})));
    EXPECT_TRUE(frustum_intersects(frustum, box_at({0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.5f})));
    // Larger than the whole volume.
    EXPECT_TRUE(frustum_intersects(frustum, box_at({0.0f, 0.0f, 0.0f}, {50.0f, 50.0f, 50.0f})));
}

TEST(Frustum, BoxTouchingAPlaneIsVisible) {
    const Frustum frustum = ortho_frustum();
    EXPECT_TRUE(frustum_intersects(frustum, Aabb{.min = {5.0f, 0.0f, 0.0f}, .max = {6.0f, 1.0f, 0.0f}}));
    EXPECT_TRUE(frustum_intersects(frustum, Aabb{.min = {-6.0f, 0.0f, 0.0f}, .max = {-5.0f, 1.0f, 0.0f}}));
    // A flat quad exactly on the near plane, as a 2D sprite at z = 1 under the default camera is.
    EXPECT_TRUE(frustum_intersects(frustum, Aabb{.min = {-1.0f, -1.0f, 1.0f}, .max = {1.0f, 1.0f, 1.0f}}));
    // Just past it is not.
    EXPECT_FALSE(frustum_intersects(frustum, Aabb{.min = {5.01f, 0.0f, 0.0f}, .max = {6.0f, 1.0f, 0.0f}}));
}

TEST(Frustum, FlatBoxUnderTheDefaultTwoDimensionalCamera) {
    const engine::Camera camera{};
    const glm::mat4 camera_world{1.0f};
    const Frustum frustum = engine::camera_frustum(camera, camera_world, engine::ui::WindowSize{800, 600});
    const Aabb quad{.min = {-0.5f, -0.5f, 0.0f}, .max = {0.5f, 0.5f, 0.0f}};
    EXPECT_TRUE(frustum_intersects(frustum, quad));
    EXPECT_FALSE(frustum_intersects(frustum, engine::render::transform_aabb(quad, glm::translate(glm::mat4{1.0f},
            glm::vec3{0.0f, 0.0f, 2.0f}))));
    // The window is 4:3, so x reaches 10 * 4 / 3.
    EXPECT_TRUE(frustum_intersects(frustum, box_at({13.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f})));
    EXPECT_FALSE(frustum_intersects(frustum, box_at({14.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f})));
}

TEST(Frustum, PerspectiveInFrontBehindBesideAndBeyondFar) {
    const Frustum frustum = perspective_frustum();
    const glm::vec3 half{0.5f};
    EXPECT_TRUE(frustum_intersects(frustum, box_at({0.0f, 0.0f, -10.0f}, half))) << "in front";
    EXPECT_FALSE(frustum_intersects(frustum, box_at({0.0f, 0.0f, 10.0f}, half))) << "behind";
    // At distance 10 the half width is 10.
    EXPECT_TRUE(frustum_intersects(frustum, box_at({9.0f, 0.0f, -10.0f}, half))) << "inside the edge";
    EXPECT_FALSE(frustum_intersects(frustum, box_at({12.0f, 0.0f, -10.0f}, half))) << "beside";
    EXPECT_FALSE(frustum_intersects(frustum, box_at({0.0f, 0.0f, -150.0f}, half))) << "beyond far";
}

TEST(Frustum, PerspectiveBoxStraddlingNearOrHoldingTheCameraIsVisible) {
    const Frustum frustum = perspective_frustum();
    EXPECT_TRUE(frustum_intersects(frustum, box_at({0.0f, 0.0f, -1.0f}, {0.5f, 0.5f, 0.5f})));
    EXPECT_TRUE(frustum_intersects(frustum, box_at({0.0f, 0.0f, 0.0f}, {5.0f, 5.0f, 5.0f})));
    // Between the camera and the near plane only: culled.
    EXPECT_FALSE(frustum_intersects(frustum, box_at({0.0f, 0.0f, -0.4f}, {0.1f, 0.1f, 0.1f})));
}

TEST(Frustum, MovedAndTurnedCameraUsesItsView) {
    engine::Camera camera{};
    camera.projection = engine::Projection::Perspective;
    camera.near_clip = 0.1f;
    camera.far_clip = 100.0f;
    // At (10, 0, 0) turned to look down -X (90 degrees about +Y turns -Z to -X).
    const glm::mat4 camera_world = engine::model_matrix(engine::Transform{
            .position = {10.0f, 0.0f, 0.0f},
            .rotation = engine::euler_xyz({0.0f, glm::half_pi<float>(), 0.0f}),
    });
    const Frustum frustum = engine::camera_frustum(camera, camera_world, engine::ui::WindowSize{800, 600});
    EXPECT_TRUE(frustum_intersects(frustum, box_at({0.0f, 0.0f, 0.0f}, glm::vec3{0.5f})));
    EXPECT_FALSE(frustum_intersects(frustum, box_at({20.0f, 0.0f, 0.0f}, glm::vec3{0.5f})));
    EXPECT_FALSE(frustum_intersects(frustum, box_at({10.0f, 0.0f, -10.0f}, glm::vec3{0.5f})));
}

TEST(Frustum, DegenerateMatrixNeverCulls) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const Aabb far_away = box_at({1000.0f, 0.0f, 0.0f}, glm::vec3{0.5f});
    EXPECT_TRUE(frustum_intersects(make_frustum(glm::mat4{nan}), far_away));
    EXPECT_TRUE(frustum_intersects(make_frustum(glm::mat4{inf}), far_away));
    // A window of width 0 under an orthographic camera divides by zero.
    const Frustum zero_width =
            engine::camera_frustum(engine::Camera{}, glm::mat4{1.0f}, engine::ui::WindowSize{0, 600});
    EXPECT_TRUE(frustum_intersects(zero_width, far_away));
}

TEST(Frustum, VisibleInAnyIsTheUnionAndAnEmptySpanCullsNothing) {
    const std::vector<Frustum> both{
            make_frustum(glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, -1.0f, 1.0f)),
            make_frustum(glm::ortho(15.0f, 25.0f, -5.0f, 5.0f, -1.0f, 1.0f)),
    };
    EXPECT_TRUE(engine::render::visible_in_any(both, box_at({0.0f, 0.0f, 0.0f}, glm::vec3{0.5f})));
    EXPECT_TRUE(engine::render::visible_in_any(both, box_at({20.0f, 0.0f, 0.0f}, glm::vec3{0.5f})));
    EXPECT_FALSE(engine::render::visible_in_any(both, box_at({10.0f, 0.0f, 0.0f}, glm::vec3{0.5f})));
    EXPECT_TRUE(engine::render::visible_in_any({}, box_at({1000.0f, 0.0f, 0.0f}, glm::vec3{0.5f})));
}

TEST(Aabb, TransformTranslatesAndScales) {
    const Aabb unit{.min = glm::vec3{-0.5f}, .max = glm::vec3{0.5f}};
    const glm::mat4 model =
            glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}), glm::vec3{2.0f, 4.0f, 6.0f});
    expect_box_near(engine::render::transform_aabb(unit, model),
            Aabb{.min = {0.0f, 0.0f, 0.0f}, .max = {2.0f, 4.0f, 6.0f}});
}

TEST(Aabb, FlipKeepsMinBelowMax) {
    const Aabb box{.min = {0.0f, 0.0f, 0.0f}, .max = {1.0f, 2.0f, 0.0f}};
    const Aabb flipped = engine::render::transform_aabb(box, glm::scale(glm::mat4{1.0f}, glm::vec3{-1.0f, -1.0f, 1.0f}));
    expect_box_near(flipped, Aabb{.min = {-1.0f, -2.0f, 0.0f}, .max = {0.0f, 0.0f, 0.0f}});
}

TEST(Aabb, RotationEnclosesTheCorners) {
    const Aabb unit{.min = {-0.5f, -0.5f, 0.0f}, .max = {0.5f, 0.5f, 0.0f}};
    const Aabb turned = engine::render::transform_aabb(
            unit, glm::rotate(glm::mat4{1.0f}, glm::quarter_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f}));
    const float half = std::sqrt(2.0f) * 0.5f;
    expect_box_near(turned, Aabb{.min = {-half, -half, 0.0f}, .max = {half, half, 0.0f}});
}

TEST(Aabb, ZeroScaleIsAPoint) {
    const Aabb unit{.min = glm::vec3{-0.5f}, .max = glm::vec3{0.5f}};
    const glm::mat4 model = glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 0.0f, 0.0f}), glm::vec3{0.0f});
    expect_box_near(engine::render::transform_aabb(unit, model),
            Aabb{.min = {3.0f, 0.0f, 0.0f}, .max = {3.0f, 0.0f, 0.0f}});
}
