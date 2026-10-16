#include <gtest/gtest.h>

#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>

#include <glm/vec4.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

class FakeMaterial final : public engine::render::IMaterial {
public:
    explicit FakeMaterial(glm::vec4 color)
        : color_(color) {}

    std::shared_ptr<engine::render::IShader> shader() const override {
        return {};
    }

    std::shared_ptr<engine::render::ITexture> texture(int) const override {
        return {};
    }

    glm::vec4 color() const override {
        return color_;
    }

    engine::render::RenderState render_state() const override {
        return {.blend = engine::render::BlendMode::Alpha};
    }

private:
    glm::vec4 color_;
};

void expect_vec4(const glm::vec4& actual, const glm::vec4& expected) {
    EXPECT_FLOAT_EQ(actual.r, expected.r);
    EXPECT_FLOAT_EQ(actual.g, expected.g);
    EXPECT_FLOAT_EQ(actual.b, expected.b);
    EXPECT_FLOAT_EQ(actual.a, expected.a);
}

}

TEST(Material, ParseValidToml) {
    constexpr std::string_view kToml = R"(
guid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
importer = "material"
shader = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
blend = "alpha"
color = [1.0, 0.5, 0.25, 1.0]
[textures]
albedo = "cccccccccccccccccccccccccccccccc"
)";

    const auto desc = engine::render::parse_material(kToml);
    ASSERT_TRUE(desc.has_value());
    EXPECT_EQ(desc->shader, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    EXPECT_EQ(desc->state.blend, engine::render::BlendMode::Alpha);
    expect_vec4(desc->color, {1.0f, 0.5f, 0.25f, 1.0f});
    EXPECT_EQ(desc->albedo, "cccccccccccccccccccccccccccccccc");
}

TEST(Material, MissingShaderFails) {
    constexpr std::string_view kToml = R"(
guid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
importer = "material"
blend = "opaque"
color = [1.0, 1.0, 1.0, 1.0]
[textures]
albedo = "cccccccccccccccccccccccccccccccc"
)";

    const auto desc = engine::render::parse_material(kToml);
    EXPECT_FALSE(desc.has_value());
}

TEST(Material, OverrideSetVec4ReplacesExistingByName) {
    engine::render::MaterialOverride override;
    override.set_vec4("uFoo", glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});
    override.set_vec4("uFoo", glm::vec4{0.0f, 1.0f, 0.0f, 1.0f});

    ASSERT_EQ(override.vec4_params.size(), 1u);
    EXPECT_EQ(override.vec4_params[0].first, "uFoo");
    expect_vec4(override.vec4_params[0].second, {0.0f, 1.0f, 0.0f, 1.0f});

    override.set_vec4("uBar", glm::vec4{0.0f, 0.0f, 1.0f, 1.0f});

    ASSERT_EQ(override.vec4_params.size(), 2u);
    EXPECT_EQ(override.vec4_params[1].first, "uBar");
    expect_vec4(override.vec4_params[1].second, {0.0f, 0.0f, 1.0f, 1.0f});
}

TEST(Material, InstanceColorMultiplies) {
    const auto material = std::make_shared<FakeMaterial>(glm::vec4{1.0f, 0.5f, 0.25f, 1.0f});
    engine::render::Renderable renderable;
    renderable.material = material;
    renderable.color = {0.5f, 1.0f, 1.0f, 0.5f};

    expect_vec4(renderable.tinted_color(), {0.5f, 0.5f, 0.25f, 0.5f});
    expect_vec4(engine::render::multiply_instance_color(material->color(), renderable.color),
            {0.5f, 0.5f, 0.25f, 0.5f});
}

namespace {

constexpr std::string_view kShaderLine = "shader = \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"\n";

std::optional<engine::render::MaterialDesc> parse_with(std::string_view extra) {
    return engine::render::parse_material(std::string(kShaderLine) + std::string(extra));
}

}

TEST(Material, StateDefaultsAreTwoDimensional) {
    const auto desc = parse_with("");
    ASSERT_TRUE(desc.has_value());
    EXPECT_EQ(desc->state.blend, engine::render::BlendMode::Opaque);
    EXPECT_FALSE(desc->state.depth_test);
    EXPECT_FALSE(desc->state.depth_write);
    EXPECT_EQ(desc->state.cull, engine::render::CullMode::None);

    const engine::render::Material material(nullptr);
    EXPECT_EQ(material.render_state(), engine::render::RenderState{});
    EXPECT_EQ(material.render_state().blend, engine::render::BlendMode::Alpha);
    EXPECT_FALSE(material.render_state().depth_test);
    EXPECT_FALSE(material.render_state().depth_write);
    EXPECT_EQ(material.render_state().cull, engine::render::CullMode::None);
}

TEST(Material, ParsesDepthAndCullKeys) {
    const auto desc = parse_with("depth_test = true\ndepth_write = true\ncull = \"back\"\n");
    ASSERT_TRUE(desc.has_value());
    EXPECT_TRUE(desc->state.depth_test);
    EXPECT_TRUE(desc->state.depth_write);
    EXPECT_EQ(desc->state.cull, engine::render::CullMode::Back);

    const auto front = parse_with("cull = \"front\"\n");
    ASSERT_TRUE(front.has_value());
    EXPECT_EQ(front->state.cull, engine::render::CullMode::Front);

    const auto none = parse_with("cull = \"none\"\n");
    ASSERT_TRUE(none.has_value());
    EXPECT_EQ(none->state.cull, engine::render::CullMode::None);
}

TEST(Material, DepthShorthandSetsBothAndSpecificKeysOverrideIt) {
    const auto both = parse_with("depth = true\n");
    ASSERT_TRUE(both.has_value());
    EXPECT_TRUE(both->state.depth_test);
    EXPECT_TRUE(both->state.depth_write);

    const auto test_only = parse_with("blend = \"alpha\"\ndepth = true\ndepth_write = false\n");
    ASSERT_TRUE(test_only.has_value());
    EXPECT_TRUE(test_only->state.depth_test);
    EXPECT_FALSE(test_only->state.depth_write);
    EXPECT_EQ(test_only->state.blend, engine::render::BlendMode::Alpha);

    const auto write_only = parse_with("depth = false\ndepth_write = true\n");
    ASSERT_TRUE(write_only.has_value());
    EXPECT_FALSE(write_only->state.depth_test);
    EXPECT_TRUE(write_only->state.depth_write);
}

TEST(Material, InvalidDepthOrCullFails) {
    EXPECT_FALSE(parse_with("cull = \"sideways\"\n").has_value());
    EXPECT_FALSE(parse_with("cull = 1\n").has_value());
    EXPECT_FALSE(parse_with("depth = \"yes\"\n").has_value());
    EXPECT_FALSE(parse_with("depth_test = 1\n").has_value());
    EXPECT_FALSE(parse_with("depth_write = \"true\"\n").has_value());
}
