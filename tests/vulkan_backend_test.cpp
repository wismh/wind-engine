#include <gtest/gtest.h>

#if defined(ENGINE_WITH_WINDOW) && !defined(__EMSCRIPTEN__)

#include "render/vulkan/sdl_vk_presentation.h"
#include "render/vulkan/vulkan_backend.h"
#include "render/vulkan/vulkan_canvas.h"
#include "render/vulkan/vulkan_context.h"
#include "render/vulkan/vulkan_factory.h"

#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>

namespace engine::render {
namespace {

TEST(VulkanBackendTest, FactoryValidatesMeshShaderAndTextures) {
    auto ctx = std::make_shared<VulkanContext>();
    VulkanFactory factory(ctx);

    MeshDesc empty_mesh{};
    EXPECT_EQ(factory.create_mesh(empty_mesh), nullptr);

    MeshDesc valid_mesh{};
    valid_mesh.vertices = {
        MeshVertex{.position = {0.0f, 0.0f, 0.0f}},
        MeshVertex{.position = {1.0f, 0.0f, 0.0f}},
        MeshVertex{.position = {0.0f, 1.0f, 0.0f}},
    };
    EXPECT_NE(factory.create_mesh(valid_mesh), nullptr);

    ShaderDesc empty_shader{};
    EXPECT_EQ(factory.create_shader(empty_shader), nullptr);

    ShaderDesc spirv_shader{};
    spirv_shader.vertex_spirv = {0x07230203u, 0x00010500u};
    spirv_shader.fragment_spirv = {0x07230203u, 0x00010500u};
    EXPECT_NE(factory.create_shader(spirv_shader), nullptr);

    TextureDesc invalid_tex{.width = 2, .height = 2, .rgba = {255, 0, 0, 255}};
    EXPECT_EQ(factory.create_texture(invalid_tex), nullptr);

    TextureDesc valid_tex{
        .width = 2,
        .height = 1,
        .rgba = {255, 0, 0, 255, 0, 255, 0, 255},
    };
    auto tex = factory.create_texture(valid_tex);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->width(), 2);
    EXPECT_EQ(tex->height(), 1);

    EXPECT_EQ(factory.create_dynamic_texture(DynamicTextureDesc{.width = 0, .height = 4}), nullptr);
    auto dyn = factory.create_dynamic_texture(DynamicTextureDesc{.width = 2, .height = 2});
    ASSERT_NE(dyn, nullptr);
    EXPECT_EQ(dyn->width(), 2);
    EXPECT_EQ(dyn->height(), 2);

    const std::vector<std::uint8_t> new_pixels(4 * 3 * 4, 128);
    EXPECT_TRUE(dyn->update(new_pixels, 4, 3));
    EXPECT_EQ(dyn->width(), 4);
    EXPECT_EQ(dyn->height(), 3);
    EXPECT_FALSE(dyn->update(new_pixels, 0, 3));
    EXPECT_EQ(dyn->width(), 4);
}

TEST(VulkanBackendTest, BackendExecutesSetViewAndCanvasPreparesVulkanWindow) {
    auto ctx = std::make_shared<VulkanContext>();
    VulkanRenderBackend backend(ctx);

    CommandBuffer commands;
    CmdSetView view{};
    view.clear_color = glm::vec4{0.2f, 0.4f, 0.6f, 1.0f};
    commands.push(view);

    backend.execute(commands);
    ASSERT_TRUE(backend.last_view().clear_color.has_value());
    EXPECT_FLOAT_EQ(backend.last_view().clear_color->r, 0.2f);

    WindowSystem window;
    VulkanCanvas canvas(window, commands, backend);
    EXPECT_EQ(canvas.prepare_window(WindowDesc{}), SDL_WINDOW_VULKAN);
    EXPECT_EQ(canvas.ui_painter(), nullptr);
    EXPECT_FALSE(canvas.set_vsync(false));
    EXPECT_FALSE(canvas.reset_ui_painter());
    canvas.render();
    canvas.present();
    const TextureDesc readback = canvas.read_pixels();
    EXPECT_EQ(readback.width, 0);
    EXPECT_EQ(readback.height, 0);
}

TEST(VulkanBackendTest, MakeSdlVkPresentationCreatesValidPresentation) {
    auto presentation = make_sdl_vk_presentation();
    ASSERT_NE(presentation, nullptr);
    EXPECT_EQ(presentation->native_window(), nullptr);
    ShaderDesc spirv_shader{};
    spirv_shader.vertex_spirv = {0x07230203u};
    spirv_shader.fragment_spirv = {0x07230203u};
    EXPECT_NE(presentation->factory().create_shader(spirv_shader), nullptr);
}

} // namespace
} // namespace engine::render

#endif
