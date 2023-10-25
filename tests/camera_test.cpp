#include <gtest/gtest.h>

#include <engine/ecs/camera.h>
#include <engine/ecs/transform.h>
#include <engine/ui/canvas.h>

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
    const engine::Transform transform{};
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
    const engine::Transform transform{};
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
    const engine::Transform transform{};
    const engine::ui::WindowSize window = window_800x600();

    const glm::vec3 top_left = engine::screen_to_world({0.f, 0.f}, camera, transform, window);
    const glm::vec3 bottom_left =
            engine::screen_to_world({0.f, static_cast<float>(window.height)}, camera, transform, window);

    EXPECT_NEAR(top_left.x, bottom_left.x, kEpsilon);
    EXPECT_GT(top_left.y, bottom_left.y);
    EXPECT_NEAR(top_left.y, camera.ortho_size, kEpsilon);
    EXPECT_NEAR(bottom_left.y, -camera.ortho_size, kEpsilon);
}
