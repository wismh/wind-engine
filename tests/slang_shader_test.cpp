#include <gtest/gtest.h>

#include <engine/render/shader_adapt.h>
#include <engine/resources/meta.h>

#include "shader_cook/parse_shader.h"
#include "shader_cook/slang_compiler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203u;

constexpr std::string_view kSampleSlang = R"(
struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
};

uniform float4x4 uModel;
uniform float4x4 uView;
uniform float4x4 uProjection;
uniform Sampler2D uTexture;
uniform float4 uColor;

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput out;
    out.vUV = input.aUV;
    out.position = mul(uProjection, mul(uView, mul(uModel, float4(input.aPosition, 1.0))));
    return out;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return uTexture.Sample(input.vUV) * uColor;
}
)";

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        static int seq = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
                ("wind_slang_" + std::to_string(stamp) + "_" + std::to_string(++seq));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void write_text(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << content;
}

} // namespace

TEST(SlangShader, CompilesSlangToSpirvAndGlsl330Core) {
#ifndef ENGINE_WITH_SLANG
    GTEST_SKIP() << "Slang compiler is not available in this build";
#else
    const auto compiled = engine::render::compile_slang_shader(kSampleSlang);
    ASSERT_TRUE(compiled.has_value()) << (compiled ? "" : compiled.error());

    ASSERT_GT(compiled->vertex_spirv.size(), 5u);
    ASSERT_GT(compiled->fragment_spirv.size(), 5u);
    EXPECT_EQ(compiled->vertex_spirv[0], kSpirvMagic);
    EXPECT_EQ(compiled->fragment_spirv[0], kSpirvMagic);

    EXPECT_EQ(compiled->vertex_src.rfind("#version 330 core", 0), 0u);
    EXPECT_EQ(compiled->fragment_src.rfind("#version 330 core", 0), 0u);

    // Standalone uniforms for OpenGL 3.3 Core.
    EXPECT_NE(compiled->vertex_src.find("uniform mat4 uModel;"), std::string::npos);
    EXPECT_NE(compiled->vertex_src.find("uniform mat4 uView;"), std::string::npos);
    EXPECT_NE(compiled->vertex_src.find("uniform mat4 uProjection;"), std::string::npos);
    EXPECT_NE(compiled->vertex_src.find("transpose(uModel)"), std::string::npos);
    EXPECT_EQ(compiled->vertex_src.find("block_GlobalParams"), std::string::npos);

    // Matching stage varying names without layout(location) on out/in varyings.
    EXPECT_NE(compiled->vertex_src.find("out vec2 _slang_v0;"), std::string::npos);
    EXPECT_NE(compiled->fragment_src.find("in vec2 _slang_v0;"), std::string::npos);
    EXPECT_NE(compiled->fragment_src.find("uniform sampler2D uTexture;"), std::string::npos);
    EXPECT_NE(compiled->fragment_src.find("uniform vec4 uColor;"), std::string::npos);

    // Adapts cleanly to GLSL 300 es while preserving SPIR-V.
    const engine::render::ShaderDesc es =
            engine::render::adapt_shader(*compiled, engine::render::ShaderTarget::Glsl300Es);
    EXPECT_EQ(es.vertex_src.rfind("#version 300 es\n", 0), 0u);
    EXPECT_EQ(es.fragment_src.rfind("#version 300 es\nprecision mediump float;\n", 0), 0u);
    EXPECT_EQ(es.vertex_spirv, compiled->vertex_spirv);
    EXPECT_EQ(es.fragment_spirv, compiled->fragment_spirv);
#endif
}

TEST(SlangShader, InvalidSyntaxReturnsDiagnostic) {
#ifndef ENGINE_WITH_SLANG
    GTEST_SKIP() << "Slang compiler is not available in this build";
#else
    const auto bad = engine::render::compile_slang_shader("this is not valid slang code {{{");
    EXPECT_FALSE(bad.has_value());
    EXPECT_FALSE(bad.error().empty());
#endif
}

TEST(SlangShader, ParseShaderXmlSupportsSlangOverrideAndLegacyGlsl) {
    // 1. Slang with native <glsl> override.
    std::string xml_with_override = "<shader lang=\"slang\"><slang><![CDATA[";
    xml_with_override.append(kSampleSlang);
    xml_with_override.append(R"(]]></slang>
        <glsl>
            <vertex>#version 330 core
            void main() { gl_Position = vec4(0.0); }</vertex>
            <fragment>#version 330 core
            out vec4 c; void main() { c = vec4(1.0); }</fragment>
        </glsl>
    </shader>)");

    const auto overridden = engine::parse_shader_xml(xml_with_override);
    ASSERT_TRUE(overridden.has_value());
#ifdef ENGINE_WITH_SLANG
    ASSERT_FALSE(overridden->vertex_spirv.empty());
    ASSERT_FALSE(overridden->fragment_spirv.empty());
    EXPECT_EQ(overridden->vertex_spirv[0], kSpirvMagic);
#endif
    EXPECT_NE(overridden->vertex_src.find("gl_Position = vec4(0.0);"), std::string::npos);
    EXPECT_NE(overridden->fragment_src.find("c = vec4(1.0);"), std::string::npos);

    // 2. Legacy raw GLSL `<xml><vertex>...</vertex><fragment>...</fragment></xml>`.
    constexpr std::string_view kLegacyXml = R"(
    <xml>
        <vertex>#version 330 core
        void main() { gl_Position = vec4(1.0); }</vertex>
        <fragment>#version 330 core
        out vec4 FragColor; void main() { FragColor = vec4(1.0); }</fragment>
    </xml>)";
    const auto legacy = engine::parse_shader_xml(kLegacyXml);
    ASSERT_TRUE(legacy.has_value());
    EXPECT_TRUE(legacy->vertex_spirv.empty());
    EXPECT_TRUE(legacy->fragment_spirv.empty());
    EXPECT_NE(legacy->vertex_src.find("gl_Position = vec4(1.0);"), std::string::npos);
}

TEST(SlangShader, CodegenScanRejectsInvalidSlangShader) {
    TempDir dir;
    write_text(dir.path / "shaders" / "broken.shader",
            "<shader lang=\"slang\"><![CDATA[\n[shader(\"vertex\")] void vertexMain() { unknown_symbol(); }\n]]></shader>");
    write_text(dir.path / "shaders" / "broken.shader.meta",
            "guid = \"0123456789abcdef0123456789abcdef\"\nimporter = \"shader\"\n");

    const auto scanned = engine::codegen_scan(dir.path);
    ASSERT_FALSE(scanned.has_value());
    EXPECT_EQ(scanned.error().kind, engine::CodegenErrorKind::Shader);
    EXPECT_NE(scanned.error().message.find("shaders/broken.shader"), std::string::npos);
}

#ifdef ENGINE_WITH_SLANG
#include "render/vulkan/particle_spirv.inc"
#include "render/vulkan/ui_spirv.inc"

TEST(SlangShader, BakedBuiltinSpirvMatchesTheCooker) {
    const std::filesystem::path dir =
            std::filesystem::path{ENGINE_SOURCE_DIR} / "tools" / "shader_cook" / "shaders";
    const auto expect_match = [](const std::filesystem::path& slang, const std::uint32_t* vertex, std::size_t vertex_count,
                                      const std::uint32_t* fragment, std::size_t fragment_count) {
        std::ifstream in(slang);
        ASSERT_TRUE(in.is_open()) << slang.generic_string();
        const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto compiled = engine::render::compile_slang_shader(source);
        ASSERT_TRUE(compiled.has_value()) << compiled.error();
        ASSERT_EQ(compiled->vertex_spirv.size(), vertex_count);
        ASSERT_EQ(compiled->fragment_spirv.size(), fragment_count);
        EXPECT_TRUE(std::equal(vertex, vertex + vertex_count, compiled->vertex_spirv.begin()));
        EXPECT_TRUE(std::equal(fragment, fragment + fragment_count, compiled->fragment_spirv.begin()));
    };
    expect_match(dir / "particle.slang", engine::render::kParticleVertexSpirv, std::size(engine::render::kParticleVertexSpirv),
            engine::render::kParticleFragmentSpirv, std::size(engine::render::kParticleFragmentSpirv));
    expect_match(dir / "ui.slang", engine::render::kUiVertexSpirv, std::size(engine::render::kUiVertexSpirv),
            engine::render::kUiFragmentSpirv, std::size(engine::render::kUiFragmentSpirv));
}
#endif
