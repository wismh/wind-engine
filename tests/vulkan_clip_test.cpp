#include <gtest/gtest.h>

#include "render/vulkan_clip.h"

#include <engine/ecs/camera.h>

#include <glm/gtc/matrix_transform.hpp>

namespace {

// The self-learning-cars camera: ortho half-height 450, near -100, far 100, sitting at z = 10.
engine::Camera cars_camera() {
    engine::Camera camera;
    camera.projection = engine::Projection::Orthographic;
    camera.ortho_size = 450.f;
    camera.auto_aspect = true;
    camera.near_clip = -100.f;
    camera.far_clip = 100.f;
    return camera;
}

glm::mat4 cars_view() {
    return glm::inverse(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 10.0f}));
}

} // namespace

TEST(VulkanClip, NegativeNearOrthoKeepsXyAndMapsGlZIntoZeroToOne) {
    const engine::ui::WindowSize window{800, 600};
    const glm::mat4 proj = engine::projection_matrix(cars_camera(), window);
    const glm::mat4 view = cars_view();
    const glm::mat4 vk_proj = engine::render::vulkan_projection(proj);

    const glm::vec4 ground = proj * view * glm::vec4{120.0f, -80.0f, 0.0f, 1.0f};
    const glm::vec4 ground_vk = vk_proj * view * glm::vec4{120.0f, -80.0f, 0.0f, 1.0f};

    EXPECT_NEAR(ground.w, 1.0f, 1e-4f);
    EXPECT_GT(ground.z, 0.0f);
    EXPECT_LT(ground.z, ground.w);
    EXPECT_NEAR(ground_vk.x, ground.x, 1e-4f);
    EXPECT_NEAR(ground_vk.y, ground.y, 1e-4f);
    EXPECT_NEAR(ground_vk.w, ground.w, 1e-4f);
    EXPECT_NEAR(ground_vk.z, 0.5f * ground.z + 0.5f * ground.w, 1e-4f);
    EXPECT_GE(ground_vk.z, 0.0f);
    EXPECT_LE(ground_vk.z, ground_vk.w);

    // Behind the camera, still inside the OpenGL slab, so GL clip z is negative.
    const glm::vec4 behind = proj * view * glm::vec4{100.0f, -200.0f, 50.0f, 1.0f};
    const glm::vec4 behind_vk = vk_proj * view * glm::vec4{100.0f, -200.0f, 50.0f, 1.0f};
    EXPECT_LT(behind.z, 0.0f);
    EXPECT_GT(behind.z, -behind.w);
    EXPECT_NEAR(behind_vk.x, behind.x, 1e-4f);
    EXPECT_NEAR(behind_vk.y, behind.y, 1e-4f);
    EXPECT_NEAR(behind_vk.w, 1.0f, 1e-4f);
    EXPECT_GE(behind_vk.z, 0.0f);
    EXPECT_LE(behind_vk.z, behind_vk.w);
}

TEST(VulkanClip, RemapDoesNotPutScaleIntoW) {
    const engine::ui::WindowSize window{800, 600};
    const glm::mat4 vk_proj = engine::render::vulkan_projection(engine::projection_matrix(cars_camera(), window));
    const glm::vec4 clip = vk_proj * cars_view() * glm::vec4{300.0f, -400.0f, 0.0f, 1.0f};
    EXPECT_NEAR(clip.w, 1.0f, 1e-4f);
    EXPECT_NEAR(clip.x, 300.0f / 600.0f, 1e-4f);
    EXPECT_NEAR(clip.y, -400.0f / 450.0f, 1e-4f);
}
