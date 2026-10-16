#include <gtest/gtest.h>

#include <engine/ecs/entity.h>
#include <engine/render/command_buffer.h>
#include <engine/render/draw_sort.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {

class FakeMesh final : public engine::render::IMesh {};

const glm::mat4 kIdentityView{1.0f};

class FakeMaterial final : public engine::render::IMaterial {
public:
    explicit FakeMaterial(engine::render::BlendMode blend = engine::render::BlendMode::Opaque)
        : state_{.blend = blend} {}
    explicit FakeMaterial(engine::render::RenderState state)
        : state_(state) {}

    std::shared_ptr<engine::render::IShader> shader() const override {
        return {};
    }

    std::shared_ptr<engine::render::ITexture> texture(int) const override {
        return {};
    }

    glm::vec4 color() const override {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    engine::render::RenderState render_state() const override {
        return state_;
    }

private:
    engine::render::RenderState state_;
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

    engine::render::sort_renderables(items, kIdentityView);

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

    engine::render::sort_renderables(items, kIdentityView);

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

    engine::render::sort_renderables(items, kIdentityView);

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

    engine::render::sort_renderables(items, kIdentityView);

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

    engine::render::sort_renderables(items, kIdentityView);

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

    engine::render::sort_renderables(items, kIdentityView);

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
    engine::render::sort_renderables(items, kIdentityView);

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

namespace {

const engine::render::RenderState kOpaque3d{
        .blend = engine::render::BlendMode::Opaque,
        .depth_test = true,
        .depth_write = true,
        .cull = engine::render::CullMode::Back,
};

const engine::render::RenderState kTransparent3d{
        .blend = engine::render::BlendMode::Alpha,
        .depth_test = true,
        .depth_write = false,
};

// A camera at z = 10 looking down -Z: a larger z is nearer.
glm::mat4 camera_view() {
    return glm::lookAt(glm::vec3{0.0f, 0.0f, 10.0f}, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
}

engine::render::RenderableItem item_at(std::shared_ptr<engine::render::IMesh> mesh,
        std::shared_ptr<engine::render::IMaterial> material, std::uint32_t index, float z, int layer = 0) {
    return {make_renderable(std::move(mesh), std::move(material), layer, 0), engine::ecs::Entity{index, 1},
            glm::vec3{0.0f, 0.0f, z}};
}

}

TEST(Sort, QueueIsOpaqueOnlyForOpaqueBlendWithDepthWrite) {
    using engine::render::BlendMode;
    using engine::render::DrawQueue;
    EXPECT_EQ(engine::render::draw_queue(kOpaque3d), DrawQueue::Opaque);
    EXPECT_EQ(engine::render::draw_queue({.blend = BlendMode::Opaque, .depth_write = true}), DrawQueue::Opaque);
    EXPECT_EQ(engine::render::draw_queue({.blend = BlendMode::Opaque}), DrawQueue::Transparent);
    EXPECT_EQ(engine::render::draw_queue({.blend = BlendMode::Alpha, .depth_test = true, .depth_write = true}),
            DrawQueue::Transparent);
    EXPECT_EQ(engine::render::draw_queue({.blend = BlendMode::Additive, .depth_write = true}),
            DrawQueue::Transparent);
    EXPECT_EQ(engine::render::draw_queue(engine::render::RenderState{}), DrawQueue::Transparent);
}

TEST(Sort, OpaqueQueueDrawsBeforeTransparentWhateverTheLayer) {
    const auto mesh = std::make_shared<FakeMesh>();
    const auto opaque = std::make_shared<FakeMaterial>(kOpaque3d);
    const auto sprite = std::make_shared<FakeMaterial>(engine::render::BlendMode::Alpha);
    std::vector<engine::render::RenderableItem> items = {
            item_at(mesh, sprite, 0, 0.0f, -100),
            item_at(mesh, opaque, 1, 0.0f, 100),
    };

    engine::render::sort_renderables(items, camera_view());

    EXPECT_EQ(items[0].entity.index, 1u);
    EXPECT_EQ(items[1].entity.index, 0u);
}

TEST(Sort, OpaqueByMaterialThenFrontToBack) {
    const auto mesh = std::make_shared<FakeMesh>();
    auto first = std::make_shared<FakeMaterial>(kOpaque3d);
    auto second = std::make_shared<FakeMaterial>(kOpaque3d);
    if (std::less<>{}(second.get(), first.get())) {
        std::swap(first, second);
    }
    std::vector<engine::render::RenderableItem> items = {
            item_at(mesh, second, 0, 5.0f),
            item_at(mesh, first, 1, -5.0f),
            item_at(mesh, first, 2, 5.0f),
            item_at(mesh, first, 3, 0.0f),
    };

    engine::render::sort_renderables(items, camera_view());

    ASSERT_EQ(items.size(), 4u);
    EXPECT_EQ(items[0].entity.index, 2u); // first material, nearest
    EXPECT_EQ(items[1].entity.index, 3u);
    EXPECT_EQ(items[2].entity.index, 1u); // first material, farthest
    EXPECT_EQ(items[3].entity.index, 0u); // second material
}

TEST(Sort, TransparentDepthTestedBackToFrontWithinLayer) {
    const auto mesh = std::make_shared<FakeMesh>();
    auto near_material = std::make_shared<FakeMaterial>(kTransparent3d);
    auto far_material = std::make_shared<FakeMaterial>(kTransparent3d);
    // Material order alone would put the near one first; depth must win.
    if (std::less<>{}(far_material.get(), near_material.get())) {
        std::swap(near_material, far_material);
    }
    std::vector<engine::render::RenderableItem> items = {
            item_at(mesh, near_material, 0, 4.0f),
            item_at(mesh, far_material, 1, -4.0f),
            item_at(mesh, near_material, 2, 0.0f, 1),
    };

    engine::render::sort_renderables(items, camera_view());

    EXPECT_EQ(items[0].entity.index, 1u);
    EXPECT_EQ(items[1].entity.index, 0u);
    EXPECT_EQ(items[2].entity.index, 2u); // a higher layer still draws after, even though it is farther
}

TEST(Sort, NotDepthTestedDrawsAfterDepthSortedInTheSameLayer) {
    const auto mesh = std::make_shared<FakeMesh>();
    const auto sprite = std::make_shared<FakeMaterial>(engine::render::BlendMode::Alpha);
    const auto glass = std::make_shared<FakeMaterial>(kTransparent3d);
    std::vector<engine::render::RenderableItem> items = {
            item_at(mesh, sprite, 0, -5.0f),
            item_at(mesh, glass, 1, 5.0f),
    };

    engine::render::sort_renderables(items, camera_view());

    EXPECT_EQ(items[0].entity.index, 1u);
    EXPECT_EQ(items[1].entity.index, 0u);
}

TEST(Sort, TwoDimensionalOrderIgnoresZ) {
    const auto mesh = std::make_shared<FakeMesh>();
    auto first = std::make_shared<FakeMaterial>(engine::render::BlendMode::Alpha);
    auto second = std::make_shared<FakeMaterial>(engine::render::BlendMode::Opaque);
    if (std::less<>{}(second.get(), first.get())) {
        std::swap(first, second);
    }
    // Same layer and order, different z, under a perspective-style view: still material, then entity.
    std::vector<engine::render::RenderableItem> items = {
            item_at(mesh, second, 0, 9.0f),
            item_at(mesh, first, 3, -9.0f),
            item_at(mesh, first, 1, 9.0f),
            item_at(mesh, second, 2, -9.0f),
    };

    engine::render::sort_renderables(items, camera_view());

    EXPECT_EQ(items[0].entity.index, 1u);
    EXPECT_EQ(items[1].entity.index, 3u);
    EXPECT_EQ(items[2].entity.index, 0u);
    EXPECT_EQ(items[3].entity.index, 2u);
}

TEST(Sort, KeyWithoutDepthTestIsNeverDepthSorted) {
    const auto glass = std::make_shared<FakeMaterial>(kOpaque3d);
    const engine::render::DrawSortKey key = engine::render::make_draw_sort_key(
            glass.get(), 0, 0, glm::vec3{0.0f, 0.0f, -3.0f}, camera_view(), engine::ecs::Entity{7, 1}, false);
    EXPECT_EQ(key.queue, engine::render::DrawQueue::Transparent);
    EXPECT_FALSE(key.depth_sorted);
    EXPECT_FLOAT_EQ(key.view_depth, 13.0f);
    EXPECT_EQ(key.entity_index, 7u);
}
