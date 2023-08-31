#include <gtest/gtest.h>

#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>

#include <glm/vec4.hpp>

#include <memory>
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

    engine::render::BlendMode blend() const override {
        return engine::render::BlendMode::Alpha;
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
    EXPECT_EQ(desc->blend, engine::render::BlendMode::Alpha);
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

TEST(Material, InstanceColorMultiplies) {
    const auto material = std::make_shared<FakeMaterial>(glm::vec4{1.0f, 0.5f, 0.25f, 1.0f});
    engine::render::Renderable renderable;
    renderable.material = material;
    renderable.color = {0.5f, 1.0f, 1.0f, 0.5f};

    expect_vec4(renderable.tinted_color(), {0.5f, 0.5f, 0.25f, 0.5f});
    expect_vec4(engine::render::multiply_instance_color(material->color(), renderable.color),
            {0.5f, 0.5f, 0.25f, 0.5f});
}
