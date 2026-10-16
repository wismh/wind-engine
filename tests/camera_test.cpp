#include <gtest/gtest.h>

#include <engine/ecs/camera.h>
#include <engine/ecs/transform.h>
#include <engine/ui/canvas.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace {

constexpr float kEpsilon = 1e-4f;

engine::ui::WindowSize window_800x600() {
    return engine::ui::WindowSize{800, 600};
}

}

TEST(Camera, OrthoBounds) {
    engine::Camera camera;
    camera.ortho_size = 10.f;
    camera.auto_aspect = true;
    const glm::mat4 transform{1.0f};
    const engine::ui::WindowSize window = window_800x600();

    const float aspect = static_cast<float>(window.width) / static_cast<float>(window.height);
    const float half_height = camera.ortho_size;
    const float half_width = half_height * aspect;

    EXPECT_NEAR(engine::camera_aspect(camera, window), aspect, kEpsilon);
    EXPECT_NEAR(aspect, 800.f / 600.f, kEpsilon);

    const glm::mat4 proj = engine::projection_matrix(camera, window);
    EXPECT_NEAR(proj[0][0], 1.f / half_width, kEpsilon);
    EXPECT_NEAR(proj[1][1], 1.f / half_height, kEpsilon);

    const glm::vec3 top_left = engine::screen_to_world({0.f, 0.f}, camera, transform, window);
    const glm::vec3 bottom_right =
            engine::screen_to_world({static_cast<float>(window.width), static_cast<float>(window.height)}, camera,
                    transform, window);

    EXPECT_NEAR(top_left.x, -half_width, kEpsilon);
    EXPECT_NEAR(top_left.y, half_height, kEpsilon);
    EXPECT_NEAR(bottom_right.x, half_width, kEpsilon);
    EXPECT_NEAR(bottom_right.y, -half_height, kEpsilon);

    const glm::vec3 center = engine::screen_to_world(
            {static_cast<float>(window.width) * 0.5f, static_cast<float>(window.height) * 0.5f}, camera, transform,
            window);
    EXPECT_NEAR(center.x, 0.f, kEpsilon);
    EXPECT_NEAR(center.y, 0.f, kEpsilon);
}

TEST(Camera, ScreenWorldRoundTrip) {
    engine::Camera camera;
    camera.ortho_size = 10.f;
    camera.auto_aspect = true;
    const glm::mat4 transform{1.0f};
    const engine::ui::WindowSize window = window_800x600();

    const glm::vec2 screen{120.f, 80.f};
    const glm::vec3 world = engine::screen_to_world(screen, camera, transform, window);
    const glm::vec2 back = engine::world_to_screen(world, camera, transform, window);
    EXPECT_NEAR(back.x, screen.x, kEpsilon);
    EXPECT_NEAR(back.y, screen.y, kEpsilon);

    const glm::vec3 world_pt{3.f, -2.f, 0.f};
    const glm::vec2 screen_pt = engine::world_to_screen(world_pt, camera, transform, window);
    const glm::vec3 world_back = engine::screen_to_world(screen_pt, camera, transform, window);
    EXPECT_NEAR(world_back.x, world_pt.x, kEpsilon);
    EXPECT_NEAR(world_back.y, world_pt.y, kEpsilon);
    EXPECT_NEAR(world_back.z, world_pt.z, kEpsilon);
}

TEST(Camera, YFlip) {
    engine::Camera camera;
    camera.ortho_size = 10.f;
    camera.auto_aspect = true;
    const glm::mat4 transform{1.0f};
    const engine::ui::WindowSize window = window_800x600();

    const glm::vec3 top_left = engine::screen_to_world({0.f, 0.f}, camera, transform, window);
    const glm::vec3 bottom_left =
            engine::screen_to_world({0.f, static_cast<float>(window.height)}, camera, transform, window);

    EXPECT_NEAR(top_left.x, bottom_left.x, kEpsilon);
    EXPECT_GT(top_left.y, bottom_left.y);
    EXPECT_NEAR(top_left.y, camera.ortho_size, kEpsilon);
    EXPECT_NEAR(bottom_left.y, -camera.ortho_size, kEpsilon);
}

namespace {

engine::Camera perspective_camera() {
    engine::Camera camera;
    camera.projection = engine::Projection::Perspective;
    camera.fov_y = glm::radians(60.f);
    camera.near_clip = 0.1f;
    camera.far_clip = 1000.f;
    return camera;
}

void expect_near(const glm::vec3& a, const glm::vec3& b, float eps = kEpsilon) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

}

TEST(Camera, DefaultsAreTheTwoDimensionalCamera) {
    const engine::Camera camera;
    EXPECT_EQ(camera.projection, engine::Projection::Orthographic);
    EXPECT_FLOAT_EQ(camera.near_clip, -1.f);
    EXPECT_FLOAT_EQ(camera.far_clip, 1.f);
    EXPECT_FLOAT_EQ(camera.fov_y, glm::radians(60.f));
}

TEST(Camera, PerspectiveProjectionMatrixValues) {
    const engine::Camera camera = perspective_camera();
    const glm::mat4 proj = engine::projection_matrix(camera, window_800x600());

    const float aspect = 800.f / 600.f;
    const float focal = 1.f / std::tan(camera.fov_y * 0.5f);
    const float near_clip = camera.near_clip;
    const float far_clip = camera.far_clip;
    EXPECT_NEAR(proj[0][0], focal / aspect, kEpsilon);
    EXPECT_NEAR(proj[1][1], focal, kEpsilon);
    EXPECT_NEAR(proj[2][2], -(far_clip + near_clip) / (far_clip - near_clip), kEpsilon);
    EXPECT_NEAR(proj[2][3], -1.f, kEpsilon);
    EXPECT_NEAR(proj[3][2], -2.f * far_clip * near_clip / (far_clip - near_clip), kEpsilon);
    EXPECT_NEAR(proj[3][3], 0.f, kEpsilon);
}

TEST(Camera, PerspectiveClampsInvalidNearAndFar) {
    engine::Camera camera = perspective_camera();
    camera.near_clip = -1.f;
    camera.far_clip = 1.f;
    engine::Camera expected = perspective_camera();
    expected.near_clip = engine::kMinPerspectiveNear;
    expected.far_clip = 1.f;
    EXPECT_EQ(engine::projection_matrix(camera, window_800x600()),
            engine::projection_matrix(expected, window_800x600()));

    camera.near_clip = 0.f;
    EXPECT_EQ(engine::projection_matrix(camera, window_800x600()),
            engine::projection_matrix(expected, window_800x600()));

    // A far plane at or before near is pushed past it.
    camera.near_clip = 5.f;
    camera.far_clip = 2.f;
    expected.near_clip = 5.f;
    expected.far_clip = 5.f + engine::kMinPerspectiveNear;
    EXPECT_EQ(engine::projection_matrix(camera, window_800x600()),
            engine::projection_matrix(expected, window_800x600()));
}

TEST(Camera, ScreenCenterRayIsCameraForwardInBothProjections) {
    const engine::ui::WindowSize window = window_800x600();
    // Camera at (1, 2, 10), turned a quarter about Y: its forward (-Z) is world -X.
    const glm::mat4 camera_world = glm::rotate(glm::translate(glm::mat4(1.f), glm::vec3{1.f, 2.f, 10.f}),
            glm::half_pi<float>(), glm::vec3{0.f, 1.f, 0.f});
    const glm::vec3 forward = -glm::vec3(camera_world[2]);
    expect_near(forward, {-1.f, 0.f, 0.f});
    const glm::vec2 center{400.f, 300.f};

    engine::Camera ortho;
    ortho.near_clip = 0.f;
    ortho.far_clip = 100.f;
    const engine::Ray ortho_ray = engine::screen_to_ray(center, ortho, camera_world, window);
    expect_near(ortho_ray.direction, forward);
    expect_near(ortho_ray.origin, {1.f, 2.f, 10.f});

    const engine::Camera perspective = perspective_camera();
    const engine::Ray perspective_ray = engine::screen_to_ray(center, perspective, camera_world, window);
    expect_near(perspective_ray.direction, forward);
    expect_near(perspective_ray.origin, glm::vec3{1.f, 2.f, 10.f} + perspective.near_clip * forward);
}

TEST(Camera, OrthoRaysAreParallelAndPerspectiveRaysSpread) {
    const engine::ui::WindowSize window = window_800x600();
    const glm::mat4 camera_world = glm::translate(glm::mat4(1.f), glm::vec3{0.f, 0.f, 10.f});

    const engine::Camera ortho;
    const engine::Ray corner = engine::screen_to_ray({0.f, 0.f}, ortho, camera_world, window);
    expect_near(corner.direction, {0.f, 0.f, -1.f});
    EXPECT_NEAR(corner.origin.x, -ortho.ortho_size * 800.f / 600.f, kEpsilon);
    EXPECT_NEAR(corner.origin.y, ortho.ortho_size, kEpsilon);

    const engine::Camera perspective = perspective_camera();
    const engine::Ray top = engine::screen_to_ray({400.f, 0.f}, perspective, camera_world, window);
    // The top edge of the screen is half the vertical field of view above forward.
    EXPECT_NEAR(std::atan2(top.direction.y, -top.direction.z), perspective.fov_y * 0.5f, 1e-3f);
    EXPECT_NEAR(top.direction.x, 0.f, kEpsilon);
}

TEST(Camera, PerspectiveScreenToWorldMeetsPlaneZZero) {
    const engine::ui::WindowSize window = window_800x600();
    const glm::mat4 camera_world = glm::translate(glm::mat4(1.f), glm::vec3{2.f, -1.f, 10.f});
    const engine::Camera camera = perspective_camera();

    const glm::vec3 center = engine::screen_to_world({400.f, 300.f}, camera, camera_world, window);
    expect_near(center, {2.f, -1.f, 0.f}, 1e-3f);

    // The top edge at distance 10 is 10 * tan(fov / 2) above the camera.
    const glm::vec3 top = engine::screen_to_world({400.f, 0.f}, camera, camera_world, window);
    EXPECT_NEAR(top.y, -1.f + 10.f * std::tan(camera.fov_y * 0.5f), 1e-3f);
    EXPECT_NEAR(top.z, 0.f, 1e-3f);
}

TEST(Camera, PerspectiveScreenToWorldWithoutPlaneHitReturnsNearPoint) {
    const engine::ui::WindowSize window = window_800x600();
    const engine::Camera camera = perspective_camera();

    // Camera at z 10 turned to face +Z: the plane z = 0 is behind it.
    const glm::mat4 away = glm::rotate(glm::translate(glm::mat4(1.f), glm::vec3{0.f, 0.f, 10.f}), glm::pi<float>(),
            glm::vec3{0.f, 1.f, 0.f});
    const engine::Ray away_ray = engine::screen_to_ray({400.f, 300.f}, camera, away, window);
    expect_near(engine::screen_to_world({400.f, 300.f}, camera, away, window), away_ray.origin);

    // Camera on the plane looking along +Y: the center ray is parallel to z = 0.
    const glm::mat4 level = glm::rotate(glm::mat4(1.f), glm::half_pi<float>(), glm::vec3{1.f, 0.f, 0.f});
    const engine::Ray level_ray = engine::screen_to_ray({400.f, 300.f}, camera, level, window);
    expect_near(level_ray.direction, {0.f, 1.f, 0.f});
    expect_near(engine::screen_to_world({400.f, 300.f}, camera, level, window), level_ray.origin);
}

TEST(Camera, PerspectiveWorldScreenRoundTrip) {
    const engine::ui::WindowSize window = window_800x600();
    const glm::mat4 camera_world = glm::translate(glm::mat4(1.f), glm::vec3{1.f, 1.f, 12.f});
    const engine::Camera camera = perspective_camera();

    const glm::vec3 world_pt{3.f, -2.f, 0.f};
    const glm::vec2 screen_pt = engine::world_to_screen(world_pt, camera, camera_world, window);
    expect_near(engine::screen_to_world(screen_pt, camera, camera_world, window), world_pt, 1e-3f);

    const glm::vec2 screen{120.f, 80.f};
    const glm::vec3 on_plane = engine::screen_to_world(screen, camera, camera_world, window);
    const glm::vec2 back = engine::world_to_screen(on_plane, camera, camera_world, window);
    EXPECT_NEAR(back.x, screen.x, 1e-2f);
    EXPECT_NEAR(back.y, screen.y, 1e-2f);

    // A point nearer the camera projects farther from the center than the same offset on z = 0.
    const glm::vec2 near_pt = engine::world_to_screen({3.f, 1.f, 6.f}, camera, camera_world, window);
    const glm::vec2 far_pt = engine::world_to_screen({3.f, 1.f, 0.f}, camera, camera_world, window);
    EXPECT_GT(near_pt.x, far_pt.x);
    EXPECT_GT(far_pt.x, 400.f);
}
