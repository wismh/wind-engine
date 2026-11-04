#include <gtest/gtest.h>

#if defined(ENGINE_WITH_WINDOW) && !defined(__EMSCRIPTEN__)

#include "render/slang_compiler.h"
#include "render/vulkan/sdl_vk_presentation.h"
#include "render/vulkan/vulkan_backend.h"
#include "render/vulkan/vulkan_canvas.h"
#include "render/vulkan/vulkan_context.h"
#include "render/vulkan/vulkan_factory.h"
#include "render/vulkan/vulkan_mesh.h"
#include "render/vulkan/vulkan_retire_queue.h"
#include "render/vulkan/vulkan_shader.h"
#include "render/vulkan/vulkan_spirv.h"
#include "render/vulkan/vulkan_texture.h"
#include "render/vulkan/vulkan_ui_device.h"
#include "render/vulkan/vulkan_ui_shader.h"
#include "resources/importers.h"

#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/material.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

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

    EXPECT_TRUE(dyn->update_rects({}, {}, 0));
    const TextureRect outside[] = {{.x = 0, .y = 0, .width = 5, .height = 1}};
    EXPECT_FALSE(dyn->update_rects(outside, new_pixels, 4));
    EXPECT_EQ(dyn->width(), 4);
    EXPECT_EQ(dyn->height(), 3);
    const TextureRect inside[] = {{.x = 1, .y = 2, .width = 2, .height = 1}};
    EXPECT_TRUE(dyn->update_rects(inside, new_pixels, 4));
    EXPECT_EQ(dyn->width(), 4);

    TextureDesc mipped = valid_tex;
    mipped.mipmaps = MipmapMode::Linear;
    auto mip_tex = std::static_pointer_cast<IDynamicTexture>(factory.create_texture(mipped));
    ASSERT_NE(mip_tex, nullptr);
    const TextureRect pixel[] = {{.x = 0, .y = 0, .width = 1, .height = 1}};
    EXPECT_FALSE(mip_tex->update_rects(pixel, mipped.rgba, 2));
}

TEST(VulkanBackendTest, MeshUpdateWithoutADeviceKeepsTheNewDataForUpload) {
    auto ctx = std::make_shared<VulkanContext>();
    VulkanFactory factory(ctx);

    MeshDesc triangle{};
    triangle.vertices.resize(3);
    auto mesh = std::static_pointer_cast<VulkanMesh>(factory.create_mesh(triangle));
    ASSERT_NE(mesh, nullptr);

    MeshDesc quad{};
    quad.vertices.resize(4);
    quad.indices = {0, 1, 2, 2, 3, 0};
    EXPECT_TRUE(mesh->update(quad));
    EXPECT_EQ(mesh->vertex_count(), 4u);
    EXPECT_EQ(mesh->index_count(), 6u);

    MeshDesc bad = quad;
    bad.indices = {0, 1, 4};
    EXPECT_FALSE(mesh->update(bad));
    EXPECT_FALSE(mesh->update(MeshDesc{}));
    EXPECT_EQ(mesh->vertex_count(), 4u);
    EXPECT_EQ(mesh->index_count(), 6u);
}

TEST(VulkanBackendTest, TextureArrayWithoutADeviceKeepsItsLayersAndRejectsABadDesc) {
    auto ctx = std::make_shared<VulkanContext>();
    VulkanFactory factory(ctx);

    TextureArrayDesc desc{.width = 2, .height = 1, .layers = 3, .mipmaps = MipmapMode::Linear};
    desc.rgba.assign(2 * 1 * 4 * 3, 200);
    const auto array = factory.create_texture_array(desc);
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array->layers(), 3);
    EXPECT_EQ(array->width(), 2);
    EXPECT_EQ(array->height(), 1);
    EXPECT_EQ(std::static_pointer_cast<VulkanTexture>(array)->mip_levels(), 2u);
    // A texture array is not dynamic.
    const std::vector<std::uint8_t> pixels(2 * 1 * 4, 0);
    EXPECT_FALSE(std::static_pointer_cast<VulkanTexture>(array)->update(pixels, 2, 1));

    TextureArrayDesc short_pixels = desc;
    short_pixels.rgba.pop_back();
    EXPECT_EQ(factory.create_texture_array(short_pixels), nullptr);
    EXPECT_EQ(factory.create_texture_array(TextureArrayDesc{}), nullptr);

    TextureDesc plain{.width = 4, .height = 4, .rgba = std::vector<std::uint8_t>(4 * 4 * 4, 1)};
    const auto texture = std::static_pointer_cast<VulkanTexture>(factory.create_texture(plain));
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->layers(), 0);
    EXPECT_EQ(texture->mip_levels(), 1u);
    plain.mipmaps = MipmapMode::Nearest;
    EXPECT_EQ(std::static_pointer_cast<VulkanTexture>(factory.create_texture(plain))->mip_levels(), 3u);
}

TEST(VulkanBackendTest, WrappedMatrixKeepsTheInnerRowMajorDecoration) {
    // A one-member wrapper struct carries the uniform name. RowMajor and the matrix stride sit on
    // the inner matrix member, which is where Slang puts them.
    constexpr std::uint32_t kWords[] = {
            0x07230203u, 0x00010500u, 0u, 8u, 0u,
            (3u << 16) | 22u, 1u, 32u,
            (4u << 16) | 23u, 2u, 1u, 4u,
            (4u << 16) | 24u, 3u, 2u, 4u,
            (3u << 16) | 30u, 4u, 3u,
            (3u << 16) | 30u, 5u, 4u,
            (4u << 16) | 32u, 6u, 2u, 5u,
            (4u << 16) | 59u, 6u, 7u, 2u,
            (3u << 16) | 71u, 5u, 2u,
            (4u << 16) | 71u, 7u, 34u, 0u,
            (4u << 16) | 71u, 7u, 33u, 0u,
            (5u << 16) | 72u, 4u, 0u, 35u, 0u,
            (5u << 16) | 72u, 4u, 0u, 7u, 16u,
            (4u << 16) | 72u, 4u, 0u, 4u,
            (5u << 16) | 72u, 5u, 0u, 35u, 0u,
            (6u << 16) | 6u, 5u, 0u, 0x6f725075u, 0x7463656au, 0x006e6f69u,
    };
    const SpirvReflection refl = reflect_spirv_program(kWords, kWords);
    const SpirvUniformMember* proj = refl.find_uniform("uProjection");
    ASSERT_NE(proj, nullptr);
    EXPECT_TRUE(proj->is_row_major);
    EXPECT_EQ(proj->matrix_stride, 16u);
    EXPECT_EQ(proj->columns, 4u);
}

TEST(VulkanBackendTest, SpirvReflectionTellsAnArraySamplerFromA2DOne) {
#if !defined(ENGINE_WITH_SLANG)
    GTEST_SKIP() << "the shaders have SPIR-V only where Slang compiled them";
#endif
    const std::filesystem::path root{ENGINE_BUILTIN_ASSETS_DIR};
    for (const std::string_view file : {"lit_array.shader", "unlit_array.shader"}) {
        const auto desc = parse_shader(read_text_file(root / "shaders" / std::string(file)));
        ASSERT_TRUE(desc.has_value()) << file << ": " << desc.error();
        const SpirvReflection refl = reflect_spirv_program(desc->vertex_spirv, desc->fragment_spirv);
        EXPECT_TRUE(refl.has_sampler) << file;
        EXPECT_TRUE(refl.sampler_arrayed) << file;
        for (const std::string_view name : {"uAlphaCutoff", "uFogEnabled", "uColor", "uModel"}) {
            EXPECT_NE(refl.find_uniform(name), nullptr) << file << " " << name;
        }
    }
    for (const std::string_view file : {"lit.shader", "unlit.shader"}) {
        const auto desc = parse_shader(read_text_file(root / "shaders" / std::string(file)));
        ASSERT_TRUE(desc.has_value()) << file;
        const SpirvReflection refl = reflect_spirv_program(desc->vertex_spirv, desc->fragment_spirv);
        EXPECT_TRUE(refl.has_sampler) << file;
        EXPECT_FALSE(refl.sampler_arrayed) << file;
    }
}

TEST(VulkanRetireQueue, HoldsAnItemUntilTheNextSubmitCompletes) {
    RetireQueue<int> queue;
    queue.retire(1);
    EXPECT_TRUE(queue.completed(0).empty());
    const std::uint64_t first = queue.submitted();
    EXPECT_EQ(queue.completed(first), std::vector<int>{1});
    EXPECT_EQ(queue.size(), 0u);
}

TEST(VulkanRetireQueue, AnOlderCompletionFreesNothingRetiredAfterIt) {
    RetireQueue<int> queue;
    const std::uint64_t first = queue.submitted();
    const std::uint64_t second = queue.submitted();
    // Retired while the third frame records: the first two frames may still read it.
    queue.retire(7);
    EXPECT_TRUE(queue.completed(first).empty());
    EXPECT_TRUE(queue.completed(second).empty());
    const std::uint64_t third = queue.submitted();
    EXPECT_EQ(queue.completed(third), std::vector<int>{7});
}

TEST(VulkanRetireQueue, ALaterSerialCoversEarlierOnesAndAnOutOfOrderOneFreesNothingNew) {
    RetireQueue<int> queue;
    queue.retire(1);
    const std::uint64_t first = queue.submitted();
    queue.retire(2);
    const std::uint64_t second = queue.submitted();
    queue.retire(3);

    // Two canvases: the second's fence is seen first. A fence covers every earlier submit on the queue.
    EXPECT_EQ(queue.completed(second), (std::vector<int>{1, 2}));
    EXPECT_TRUE(queue.completed(first).empty());
    EXPECT_EQ(queue.size(), 1u);
    EXPECT_EQ(queue.take_all(), std::vector<int>{3});
    EXPECT_EQ(queue.size(), 0u);
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

#if defined(ENGINE_BUILTIN_ASSETS_DIR) && defined(ENGINE_WITH_SLANG)
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
    const SpirvUniformMember* model = unlit_refl.find_uniform("uModel");
    const SpirvUniformMember* view_u = unlit_refl.find_uniform("uView");
    const SpirvUniformMember* proj = unlit_refl.find_uniform("uProjection");
    ASSERT_NE(model, nullptr);
    ASSERT_NE(view_u, nullptr);
    ASSERT_NE(proj, nullptr);
    // This Slang decorates the matrices RowMajor and does not emit OpTranspose. write_mat4
    // transposes GLM's columns into that layout; a column-major upload would put translation in w.
    EXPECT_TRUE(model->is_row_major);
    EXPECT_TRUE(view_u->is_row_major);
    EXPECT_TRUE(proj->is_row_major);
    int transposes = 0;
    const auto& words = unlit_desc->vertex_spirv;
    for (std::size_t i = 5; i < words.size();) {
        const std::uint32_t header = words[i];
        const auto opcode = static_cast<std::uint16_t>(header & 0xFFFFu);
        const auto count = static_cast<std::uint16_t>(header >> 16);
        if (count == 0 || i + count > words.size()) {
            break;
        }
        if (opcode == 84) {
            ++transposes;
        }
        i += count;
    }
    EXPECT_EQ(transposes, 0);
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

    // Cutout and fog uniforms, which the backend writes for every mesh draw.
    for (const SpirvReflection* refl : {&unlit_refl, &lit_refl}) {
        for (const std::string_view name : {"uAlphaCutoff", "uFogEnabled", "uFogColor", "uFogRange"}) {
            EXPECT_NE(refl->find_uniform(name), nullptr) << name;
        }
    }
}
#endif

#if defined(ENGINE_WITH_SLANG)
TEST(VulkanBackendTest, UiShaderCompilesAndUiDeviceTracksDrawCallsAndTextures) {
    auto ctx = std::make_shared<VulkanContext>();
    VulkanRenderBackend backend(ctx);
    VulkanUiDevice ui_device(ctx);
    ASSERT_TRUE(ui_device.create());
    ASSERT_NE(ui_device.shader(), nullptr);
    EXPECT_TRUE(ui_device.shader()->valid());
    EXPECT_FALSE(ui_device.shader()->reflection().vertex_entry.empty());
    EXPECT_FALSE(ui_device.shader()->reflection().fragment_entry.empty());
    EXPECT_NE(ui_device.shader()->reflection().find_uniform("u_view_size"), nullptr);

    ui::UiRenderer renderer(backend.ui_resources(), ui_device);
    constexpr AssetId kTestImage{"d1a1c2d3e4f5678901234567890abc11"};
    TextureDesc img_desc{};
    img_desc.width = 4;
    img_desc.height = 4;
    img_desc.rgba.assign(4u * 4u * 4u, 255u);
    EXPECT_TRUE(renderer.add_image(kTestImage, img_desc));

    ui::Gradient grad{};
    grad.stops = {
        ui::GradientStop{.color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}, .percent = 0.0f},
        ui::GradientStop{.color = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}, .percent = 100.0f},
    };

    renderer.begin_frame(800.0f, 600.0f);
    renderer.fill_rounded_rect(Rect{10.0f, 20.0f, 100.0f, 50.0f}, 4.0f, glm::vec4{1.0f});
    renderer.fill_rounded_rect_gradient(Rect{20.0f, 30.0f, 80.0f, 40.0f}, 2.0f, grad);
    renderer.image(kTestImage, Rect{120.0f, 20.0f, 16.0f, 16.0f});
    renderer.end_frame();
    EXPECT_EQ(ui_device.draw_calls(), 1);
    EXPECT_GT(ui_device.texture_page_count(ui::UiTextureSlot::Ramp), 0u);
    EXPECT_GT(ui_device.texture_page_count(ui::UiTextureSlot::Image), 0u);

    ui_device.release_textures();
    EXPECT_EQ(ui_device.texture_page_count(ui::UiTextureSlot::Ramp), 0u);
    EXPECT_EQ(ui_device.texture_page_count(ui::UiTextureSlot::Image), 0u);
}
#endif

} // namespace
} // namespace engine::render

#endif
