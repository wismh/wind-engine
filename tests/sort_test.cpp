#include <gtest/gtest.h>

#include <engine/ecs/entity.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>

#include <glm/vec4.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace {

class FakeMesh final : public engine::render::IMesh {};

class FakeMaterial final : public engine::render::IMaterial {
public:
    explicit FakeMaterial(engine::render::BlendMode blend = engine::render::BlendMode::Opaque)
        : blend_(blend) {}

    std::shared_ptr<engine::render::IShader> Shader() const override {
        return {};
    }

    std::shared_ptr<engine::render::ITexture> Texture(int) const override {
        return {};
    }

    glm::vec4 Color() const override {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    engine::render::BlendMode Blend() const override {
        return blend_;
    }

private:
    engine::render::BlendMode blend_;
};

engine::render::Renderable make_renderable(std::shared_ptr<engine::render::IMesh> mesh,
                                           std::shared_ptr<engine::render::IMaterial> material, int layer = 0,
                                           int order_in_layer = 0) {
    return engine::render::Renderable{
            std::move(mesh),
            std::move(material),
            glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            layer,
            order_in_layer,
    };
}

}

TEST(Sort, ByLayer) {
    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    std::vector<engine::render::RenderableItem> items = {
            {make_renderable(mesh, material, 10, 0), engine::ecs::Entity{0, 1}},
            {make_renderable(mesh, material, 0, 0), engine::ecs::Entity{1, 1}},
    };

    engine::render::sort_renderables(items);

    EXPECT_EQ(items[0].renderable.layer, 0);
    EXPECT_EQ(items[0].entity.index, 1u);
    EXPECT_EQ(items[1].renderable.layer, 10);
}

TEST(Sort, ThenOrderInLayer) {
    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    std::vector<engine::render::RenderableItem> items = {
            {make_renderable(mesh, material, 2, 7), engine::ecs::Entity{0, 1}},
            {make_renderable(mesh, material, 2, 1), engine::ecs::Entity{1, 1}},
    };

    engine::render::sort_renderables(items);

    EXPECT_EQ(items[0].renderable.order_in_layer, 1);
    EXPECT_EQ(items[1].renderable.order_in_layer, 7);
}

TEST(Sort, ThenMaterialPointer) {
    const auto mesh = std::make_shared<FakeMesh>();
    const auto material_a = std::make_shared<FakeMaterial>();
    const auto material_b = std::make_shared<FakeMaterial>();
    std::shared_ptr<engine::render::IMaterial> first = material_a;
    std::shared_ptr<engine::render::IMaterial> second = material_b;
    if (std::less<>{}(material_b.get(), material_a.get())) {
        first = material_b;
        second = material_a;
    }

    std::vector<engine::render::RenderableItem> items = {
            {make_renderable(mesh, second, 0, 0), engine::ecs::Entity{0, 1}},
            {make_renderable(mesh, first, 0, 0), engine::ecs::Entity{1, 1}},
    };

    engine::render::sort_renderables(items);

    EXPECT_EQ(items[0].renderable.material, first);
    EXPECT_EQ(items[1].renderable.material, second);
}

TEST(Sort, ThenEntityIndex) {
    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    std::vector<engine::render::RenderableItem> items = {
            {make_renderable(mesh, material, 0, 0), engine::ecs::Entity{4, 1}},
            {make_renderable(mesh, material, 0, 0), engine::ecs::Entity{1, 1}},
    };

    engine::render::sort_renderables(items);

    EXPECT_EQ(items[0].entity.index, 1u);
    EXPECT_EQ(items[1].entity.index, 4u);
}

TEST(Sort, StableEqualKeys) {
    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    const engine::render::Renderable renderable = make_renderable(mesh, material, 3, 5);
    std::vector<engine::render::RenderableItem> items = {
            {renderable, engine::ecs::Entity{8, 10}},
            {renderable, engine::ecs::Entity{8, 20}},
    };

    engine::render::sort_renderables(items);

    EXPECT_EQ(items[0].entity.generation, 10u);
    EXPECT_EQ(items[1].entity.generation, 20u);
}

TEST(Sort, BlendDoesNotReorder) {
    const auto mesh = std::make_shared<FakeMesh>();
    const auto opaque = std::make_shared<FakeMaterial>(engine::render::BlendMode::Opaque);
    const auto additive = std::make_shared<FakeMaterial>(engine::render::BlendMode::Additive);

    std::vector<engine::render::RenderableItem> items = {
            {make_renderable(mesh, additive, 0, 0), engine::ecs::Entity{0, 1}},
            {make_renderable(mesh, opaque, 0, 0), engine::ecs::Entity{0, 1}},
    };

    engine::render::sort_renderables(items);

    EXPECT_TRUE(std::less<>{}(items[0].renderable.material.get(), items[1].renderable.material.get()));
    EXPECT_EQ(items[0].renderable.layer, items[1].renderable.layer);
    EXPECT_EQ(items[0].renderable.order_in_layer, items[1].renderable.order_in_layer);
}

TEST(Sort, WorldThenUiKeepsUiLast) {
    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    std::vector<engine::render::RenderableItem> items = {
            {make_renderable(mesh, material, 5, 0), engine::ecs::Entity{0, 1}},
            {make_renderable(mesh, material, 1, 0), engine::ecs::Entity{1, 1}},
    };
    engine::render::sort_renderables(items);

    engine::render::CommandBuffer buffer;
    for (const engine::render::RenderableItem& item : items) {
        engine::render::CmdDrawMesh cmd;
        cmd.mesh = item.renderable.mesh;
        cmd.material = item.renderable.material;
        buffer.push(std::move(cmd));
    }
    buffer.push(engine::render::CmdDrawUI{{0.0f, 0.0f, 64.0f, 32.0f}});

    ASSERT_EQ(buffer.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[0]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[1]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(buffer[2]));
    EXPECT_EQ(items[0].renderable.layer, 1);
    EXPECT_EQ(items[1].renderable.layer, 5);
}
