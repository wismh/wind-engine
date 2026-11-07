#include <gtest/gtest.h>

#include <engine/resources/shader_blob.h>
#include <engine/resources/shader_cook.h>

#include "gltf/cook.h"
#include "shader_cook/cook.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

namespace {

struct RegisterCookers {
    RegisterCookers() {
        engine::gltf::register_gltf_cooker();
        engine::shader_cook::register_shader_cooker();
    }
};

const RegisterCookers kRegisterCookers;

}

TEST(ShaderCook, BlobRoundTripsGlslAndSpirv) {
    constexpr std::string_view kXml = R"(
        <xml>
            <vertex>#version 330 core
            void main() { gl_Position = vec4(1.0); }</vertex>
            <fragment>#version 330 core
            out vec4 FragColor; void main() { FragColor = vec4(1.0); }</fragment>
        </xml>)";
    const auto cooked = engine::cook_shader_asset(kXml, {});
    ASSERT_TRUE(cooked.has_value()) << cooked.error();
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(cooked->data()), 4), "WSHD");

    const auto desc = engine::decode_shader_blob(
            std::span<const std::uint8_t>(cooked->data(), cooked->size()));
    ASSERT_TRUE(desc.has_value()) << desc.error();
    EXPECT_NE(desc->vertex_src.find("gl_Position = vec4(1.0);"), std::string::npos);
    EXPECT_NE(desc->fragment_src.find("FragColor = vec4(1.0);"), std::string::npos);
    EXPECT_TRUE(desc->vertex_spirv.empty());
    EXPECT_TRUE(desc->fragment_spirv.empty());
}

TEST(ShaderCook, BuiltinDecalShaderCooksProjectionUniforms) {
    const std::filesystem::path file =
            std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "shaders" / "decal.shader";
    std::ifstream in(file);
    ASSERT_TRUE(in.is_open()) << file.generic_string();
    const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto cooked = engine::cook_shader_asset(xml, file.parent_path());
    ASSERT_TRUE(cooked.has_value()) << cooked.error();
    const auto desc = engine::decode_shader_blob(std::span<const std::uint8_t>(cooked->data(), cooked->size()));
    ASSERT_TRUE(desc.has_value()) << desc.error();
    EXPECT_NE(desc->fragment_src.find("uDepth"), std::string::npos);
    EXPECT_NE(desc->fragment_src.find("uInvViewProj"), std::string::npos);
    EXPECT_NE(desc->fragment_src.find("uDecalAxis"), std::string::npos);
    EXPECT_NE(desc->fragment_src.find("uHasDepth"), std::string::npos);
}

TEST(ShaderCook, RejectsABlobThatIsNotWshd) {
    const std::uint8_t bytes[] = {'n', 'o', 'p', 'e'};
    const auto desc = engine::decode_shader_blob(bytes);
    EXPECT_FALSE(desc.has_value());
}
