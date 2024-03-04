#include <gtest/gtest.h>

#include <engine/render/shader_adapt.h>

#include <string>

namespace {

constexpr std::string_view kUnlitVertex = R"(#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;

out vec2 vUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    vUV = aUV;
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
)";

constexpr std::string_view kUnlitFragment = R"(#version 330 core

out vec4 FragColor;

in vec2 vUV;
uniform sampler2D uTexture;
uniform vec4 uColor;

void main() {
    FragColor = texture(uTexture, vUV) * uColor;
}
)";

}

TEST(ShaderAdapt, Glsl330TargetIsPassthrough) {
    engine::render::ShaderDesc src;
    src.vertex_src = "#version 330 core\nvoid main() {}";
    src.fragment_src = "#version 330 core\nvoid main() { FragColor = vec4(1.0); }";

    const engine::render::ShaderDesc out =
            engine::render::adapt_shader(src, engine::render::ShaderTarget::Glsl330Core);
    EXPECT_EQ(out.vertex_src, src.vertex_src);
    EXPECT_EQ(out.fragment_src, src.fragment_src);
}

TEST(ShaderAdapt, Rewrites330CoreTo300Es) {
    const std::string vs = engine::render::adapt_glsl(kUnlitVertex, engine::render::ShaderTarget::Glsl300Es, false);
    const std::string fs = engine::render::adapt_glsl(kUnlitFragment, engine::render::ShaderTarget::Glsl300Es, true);

    EXPECT_NE(vs.find("#version 300 es"), std::string::npos);
    EXPECT_EQ(vs.find("#version 330"), std::string::npos);
    EXPECT_NE(fs.find("#version 300 es"), std::string::npos);
    EXPECT_EQ(fs.find("#version 330"), std::string::npos);
    EXPECT_NE(fs.find("precision mediump float;"), std::string::npos);
    EXPECT_NE(vs.find("layout(location = 0)"), std::string::npos);
    EXPECT_NE(fs.find("texture("), std::string::npos);
}

TEST(ShaderAdapt, Texture2DBecomesTexture) {
    const std::string fs = engine::render::adapt_glsl("#version 330 core\nvoid main() { gl_FragColor = texture2D(uTexture, vUV); }",
            engine::render::ShaderTarget::Glsl300Es, true);
    EXPECT_EQ(fs.find("texture2D("), std::string::npos);
    EXPECT_NE(fs.find("texture("), std::string::npos);
}

TEST(ShaderAdapt, IdempotentOnEs300) {
    engine::render::ShaderDesc src;
    src.vertex_src = std::string(kUnlitVertex);
    src.fragment_src = std::string(kUnlitFragment);
    const engine::render::ShaderDesc once =
            engine::render::adapt_shader(src, engine::render::ShaderTarget::Glsl300Es);
    const engine::render::ShaderDesc twice =
            engine::render::adapt_shader(once, engine::render::ShaderTarget::Glsl300Es);
    EXPECT_EQ(once.vertex_src, twice.vertex_src);
    EXPECT_EQ(once.fragment_src, twice.fragment_src);
}

TEST(ShaderAdapt, InsertsVersionWhenMissing) {
    const std::string vs = engine::render::adapt_glsl("void main() {}", engine::render::ShaderTarget::Glsl300Es, false);
    EXPECT_EQ(vs.rfind("#version 300 es", 0), 0u);
}

TEST(ShaderAdapt, DoesNotDuplicatePrecision) {
    const std::string src = "#version 300 es\nprecision mediump float;\nvoid main() {}\n";
    const std::string fs = engine::render::adapt_glsl(src, engine::render::ShaderTarget::Glsl300Es, true);
    const std::size_t first = fs.find("precision mediump float;");
    const std::size_t second = fs.find("precision mediump float;", first + 1);
    EXPECT_NE(first, std::string::npos);
    EXPECT_EQ(second, std::string::npos);
}

TEST(ShaderAdapt, AdaptShaderRewritesBothStages) {
    engine::render::ShaderDesc src;
    src.vertex_src = std::string(kUnlitVertex);
    src.fragment_src = std::string(kUnlitFragment);
    const engine::render::ShaderDesc out =
            engine::render::adapt_shader(src, engine::render::ShaderTarget::Glsl300Es);
    EXPECT_NE(out.vertex_src.find("#version 300 es"), std::string::npos);
    EXPECT_NE(out.fragment_src.find("#version 300 es"), std::string::npos);
    EXPECT_NE(out.fragment_src.find("precision mediump float;"), std::string::npos);
}
