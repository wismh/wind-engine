#include <gtest/gtest.h>

#if defined(ENGINE_WITH_WINDOW) && !defined(__EMSCRIPTEN__)

#include "render/slang_compiler.h"
#include "render/vulkan/sdl_vk_presentation.h"
#include "render/vulkan/vulkan_backend.h"
#include "render/vulkan/vulkan_canvas.h"
#include "render/vulkan/vulkan_context.h"
#include "render/vulkan/vulkan_factory.h"
#include "render/vulkan/vulkan_shader.h"
#include "render/vulkan/vulkan_spirv.h"
#include "resources/importers.h"

#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/material.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace engine::render {
namespace {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

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

TEST(VulkanBackendTest, BackendExecutesSetViewMeshAndParticlesSafelyWithoutPass) {
    auto ctx = std::make_shared<VulkanContext>();
    VulkanFactory factory(ctx);
    VulkanRenderBackend backend(ctx);

    MeshDesc valid_mesh{};
    valid_mesh.vertices = {
        MeshVertex{.position = {0.0f, 0.0f, 0.0f}},
        MeshVertex{.position = {1.0f, 0.0f, 0.0f}},
        MeshVertex{.position = {0.0f, 1.0f, 0.0f}},
    };
    auto mesh = factory.create_mesh(valid_mesh);
    ShaderDesc spirv_shader{};
    spirv_shader.vertex_spirv = {0x07230203u, 0x00010500u};
    spirv_shader.fragment_spirv = {0x07230203u, 0x00010500u};
    auto shader = factory.create_shader(spirv_shader);
    auto material = std::make_shared<Material>(shader);

    CommandBuffer commands;
    CmdSetView view{};
    view.clear_color = glm::vec4{0.2f, 0.4f, 0.6f, 1.0f};
    commands.push(view);
    commands.push(CmdDrawMesh{.mesh = mesh, .material = material});
    commands.push(CmdDrawParticles{
        .material = material,
        .instances = {ParticleInstance{}},
    });

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

#ifdef ENGINE_BUILTIN_ASSETS_DIR
TEST(VulkanBackendTest, SpirvReflectionExtractsUniformsFromBuiltinSlangShaders) {
    const std::filesystem::path root{ENGINE_BUILTIN_ASSETS_DIR};
    const auto unlit_desc = parse_shader(read_text_file(root / "shaders" / "unlit.shader"));
    ASSERT_TRUE(unlit_desc.has_value());

    const SpirvReflection unlit_refl =
        reflect_spirv_program(unlit_desc->vertex_spirv, unlit_desc->fragment_spirv);
    EXPECT_FALSE(unlit_refl.vertex_entry.empty());
    EXPECT_FALSE(unlit_refl.fragment_entry.empty());
    EXPECT_TRUE(unlit_refl.has_ubo);
    EXPECT_GT(unlit_refl.ubo_size, 0u);
    EXPECT_TRUE(unlit_refl.has_sampler);
    EXPECT_NE(unlit_refl.find_uniform("uModel"), nullptr);
    EXPECT_NE(unlit_refl.find_uniform("uView"), nullptr);
    EXPECT_NE(unlit_refl.find_uniform("uProjection"), nullptr);
    EXPECT_NE(unlit_refl.find_uniform("uColor"), nullptr);
    EXPECT_NE(unlit_refl.find_uniform("uUvScale"), nullptr);
    EXPECT_NE(unlit_refl.find_uniform("uUvOffset"), nullptr);

    const auto lit_desc = parse_shader(read_text_file(root / "shaders" / "lit.shader"));
    ASSERT_TRUE(lit_desc.has_value());
    const SpirvReflection lit_refl =
        reflect_spirv_program(lit_desc->vertex_spirv, lit_desc->fragment_spirv);
    EXPECT_TRUE(lit_refl.has_ubo);
    EXPECT_TRUE(lit_refl.has_sampler);
    ASSERT_NE(lit_refl.find_uniform("uNormalMatrix"), nullptr);
    EXPECT_EQ(lit_refl.find_uniform("uNormalMatrix")->columns, 3u);
    EXPECT_NE(lit_refl.find_uniform("uLightCount"), nullptr);
    ASSERT_NE(lit_refl.find_uniform("uLightDirections"), nullptr);
    EXPECT_EQ(lit_refl.find_uniform("uLightDirections")->array_elements, 4u);
    EXPECT_GE(lit_refl.find_uniform("uLightDirections")->array_stride, 16u);
    ASSERT_NE(lit_refl.find_uniform("uLightColors"), nullptr);
    EXPECT_EQ(lit_refl.find_uniform("uLightColors")->array_elements, 4u);
    EXPECT_NE(lit_refl.find_uniform("uAmbient"), nullptr);
}
#endif

} // namespace
} // namespace engine::render

#endif
