#include <gtest/gtest.h>

#include <engine/core/bound_windows.h>
#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/decal.h>
#include <engine/render/draw_sort.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/primitives.h>
#include <engine/render/renderable.h>
#include <engine/ui/presentation.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

class FakeMesh final : public engine::render::IMesh {
public:
    FakeMesh() : engine::render::IMesh(engine::render::make_cube(1.0f)) {}
    explicit FakeMesh(const engine::render::MeshDesc& desc) : engine::render::IMesh(desc) {}

protected:
    bool replace(const engine::render::MeshDesc&) override {
        return true;
    }
};

class FakeMaterial final : public engine::render::IMaterial {
public:
    explicit FakeMaterial(engine::render::BlendMode blend = engine::render::BlendMode::Alpha,
                          bool depth_write = false)
        : state_{.blend = blend, .depth_test = true, .depth_write = depth_write} {}

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
    engine::render::RenderState state_{};
};

void spawn_camera(engine::ecs::World& world) {
    const engine::ecs::Entity camera = world.create();
    world.emplace<engine::Transform>(camera, engine::Transform{});
    world.emplace<engine::Camera>(camera, engine::Camera{});
    world.ctx<engine::ActiveCamera>().entity = camera;
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};
}

void spawn_perspective_camera(engine::ecs::World& world) {
    spawn_camera(world);
    const engine::ecs::Entity camera = world.ctx<engine::ActiveCamera>().entity;
    world.get<engine::Transform>(camera).position = {0.0f, 0.0f, 10.0f};
    engine::Camera& lens = world.get<engine::Camera>(camera);
    lens.projection = engine::Projection::Perspective;
    lens.near_clip = 0.1f;
    lens.far_clip = 100.0f;
}

} // namespace

TEST(DecalTest, DefaultsMatchSpecification) {
    const engine::render::Decal decal;
    EXPECT_EQ(decal.mesh, nullptr);
    EXPECT_EQ(decal.material, nullptr);
    EXPECT_EQ(decal.texture, nullptr);
    EXPECT_EQ(decal.color, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_EQ(decal.uv_scale, glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(decal.uv_offset, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(decal.layer, 0);
    EXPECT_EQ(decal.order_in_layer, 0);
    EXPECT_FLOAT_EQ(decal.angle_cutoff, 0.0f);
    EXPECT_FLOAT_EQ(decal.normal_fade, 0.2f);
    EXPECT_TRUE(decal.frustum_cull);
    EXPECT_FALSE(decal.material_override.has_value());
}

TEST(DecalTest, MaterialOverrideParamsRoundTrip) {
    engine::render::Decal decal;
    decal.set_param("uDecalParam", glm::vec4{1.0f, 2.0f, 3.0f, 4.0f});
    ASSERT_TRUE(decal.material_override.has_value());
    EXPECT_EQ(decal.material_override->vec4_params.size(), 1u);
    EXPECT_EQ(decal.material_override->vec4_params[0].first, "uDecalParam");
    EXPECT_EQ(decal.material_override->vec4_params[0].second, glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));

    decal.clear_overrides();
    EXPECT_FALSE(decal.material_override.has_value());
}

TEST(DecalTest, DecalQueueSortsBetweenOpaqueAndTransparent) {
    const FakeMaterial opaque_mat{engine::render::BlendMode::Opaque, true};
    const FakeMaterial decal_mat{engine::render::BlendMode::Alpha, false};
    const FakeMaterial transparent_mat{engine::render::BlendMode::Alpha, false};

    const glm::mat4 view(1.0f);
    const engine::ecs::Entity e1{1, 1};
    const engine::ecs::Entity e2{2, 1};
    const engine::ecs::Entity e3{3, 1};

    const engine::render::DrawSortKey opaque_key = engine::render::make_draw_sort_key(
            &opaque_mat, 0, 0, glm::vec3(0.0f, 0.0f, -5.0f), view, e1, true);
    const engine::render::DrawSortKey decal_key = engine::render::make_decal_draw_sort_key(
            &decal_mat, 0, 0, glm::vec3(0.0f, 0.0f, -5.0f), view, e2);
    const engine::render::DrawSortKey transparent_key = engine::render::make_draw_sort_key(
            &transparent_mat, 0, 0, glm::vec3(0.0f, 0.0f, -5.0f), view, e3, true);

    EXPECT_EQ(opaque_key.queue, engine::render::DrawQueue::Opaque);
    EXPECT_EQ(decal_key.queue, engine::render::DrawQueue::Decal);
    EXPECT_EQ(transparent_key.queue, engine::render::DrawQueue::Transparent);

    EXPECT_TRUE(engine::render::draw_sort_less(opaque_key, decal_key));
    EXPECT_TRUE(engine::render::draw_sort_less(decal_key, transparent_key));
    EXPECT_TRUE(engine::render::draw_sort_less(opaque_key, transparent_key));
}

TEST(DecalTest, DecalsSortByLayerThenOrderThenMaterialThenDepth) {
    const FakeMaterial mat_a{engine::render::BlendMode::Alpha, false};
    const FakeMaterial mat_b{engine::render::BlendMode::Alpha, false};
    const glm::mat4 view(1.0f);
    const engine::ecs::Entity e1{1, 1};
    const engine::ecs::Entity e2{2, 1};

    // Layer comparison
    const auto key_layer0 = engine::render::make_decal_draw_sort_key(&mat_a, 0, 0, {0.0f, 0.0f, -5.0f}, view, e1);
    const auto key_layer1 = engine::render::make_decal_draw_sort_key(&mat_a, 1, 0, {0.0f, 0.0f, -5.0f}, view, e2);
    EXPECT_TRUE(engine::render::draw_sort_less(key_layer0, key_layer1));

    // Order in layer comparison
    const auto key_order0 = engine::render::make_decal_draw_sort_key(&mat_a, 0, 0, {0.0f, 0.0f, -5.0f}, view, e1);
    const auto key_order1 = engine::render::make_decal_draw_sort_key(&mat_a, 0, 1, {0.0f, 0.0f, -5.0f}, view, e2);
    EXPECT_TRUE(engine::render::draw_sort_less(key_order0, key_order1));

    // View depth comparison (front to back)
    const auto key_near = engine::render::make_decal_draw_sort_key(&mat_a, 0, 0, {0.0f, 0.0f, -2.0f}, view, e1);
    const auto key_far = engine::render::make_decal_draw_sort_key(&mat_a, 0, 0, {0.0f, 0.0f, -10.0f}, view, e2);
    EXPECT_TRUE(engine::render::draw_sort_less(key_near, key_far));
}

TEST(DecalTest, RunRenderPushesCmdDrawDecalWithInvertedModelMatrix) {
    engine::ecs::World world;
    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto mesh = std::make_shared<FakeMesh>();
    const auto mat = std::make_shared<FakeMaterial>();

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{
        .position = {2.0f, 3.0f, -5.0f},
        .scale = {2.0f, 2.0f, 2.0f},
    });
    world.emplace<engine::render::Decal>(entity, engine::render::Decal{
        .mesh = mesh,
        .material = mat,
        .color = {1.0f, 0.5f, 0.25f, 1.0f},
        .uv_scale = {2.0f, 2.0f},
        .uv_offset = {0.1f, 0.2f},
        .angle_cutoff = 0.3f,
        .normal_fade = 0.1f,
    });

    world.run(engine::ecs::Schedule::Frame);

    // Buffer should have: CmdSetView, CmdDrawDecal, CmdPostProcess (since decals activate post depth target)
    ASSERT_GE(commands.size(), 3u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdSetView>(commands[0]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawDecal>(commands[1]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdPostProcess>(commands[2]));

    const auto& cmd = std::get<engine::render::CmdDrawDecal>(commands[1]);
    EXPECT_EQ(cmd.mesh, mesh);
    EXPECT_EQ(cmd.material, mat);
    EXPECT_EQ(cmd.color, glm::vec4(1.0f, 0.5f, 0.25f, 1.0f));
    EXPECT_EQ(cmd.uv_scale, glm::vec2(2.0f, 2.0f));
    EXPECT_EQ(cmd.uv_offset, glm::vec2(0.1f, 0.2f));
    EXPECT_FLOAT_EQ(cmd.angle_cutoff, 0.3f);
    EXPECT_FLOAT_EQ(cmd.normal_fade, 0.1f);

    // Check model and inv_model
    const glm::mat4 expected_model = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, -5.0f)) *
                                     glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 2.0f));
    EXPECT_EQ(cmd.model, expected_model);
    EXPECT_FLOAT_EQ(cmd.inv_model[3][0], glm::inverse(expected_model)[3][0]);
    EXPECT_FLOAT_EQ(cmd.inv_model[3][1], glm::inverse(expected_model)[3][1]);
    EXPECT_FLOAT_EQ(cmd.inv_model[3][2], glm::inverse(expected_model)[3][2]);
}

TEST(DecalTest, FrustumCullingDropsOffscreenDecalUnlessOptedOut) {
    engine::ecs::World world;
    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto mesh = std::make_shared<FakeMesh>();
    const auto mat = std::make_shared<FakeMaterial>();

    // Entity far outside the view (1000 units to the right)
    const engine::ecs::Entity culled = world.create();
    world.emplace<engine::Transform>(culled, engine::Transform{
        .position = {1000.0f, 0.0f, -5.0f},
        .scale = {1.0f, 1.0f, 1.0f},
    });
    world.emplace<engine::render::Decal>(culled, engine::render::Decal{
        .mesh = mesh,
        .material = mat,
        .frustum_cull = true,
    });

    world.run(engine::ecs::Schedule::Frame);

    // Should only have CmdSetView and CmdPostProcess, but no CmdDrawDecal
    for (const auto& cmd : commands) {
        EXPECT_FALSE(std::holds_alternative<engine::render::CmdDrawDecal>(cmd));
    }

    // Now turn off frustum_cull
    world.get<engine::render::Decal>(culled).frustum_cull = false;
    commands.clear();
    world.run(engine::ecs::Schedule::Frame);

    bool found_decal = false;
    for (const auto& cmd : commands) {
        if (std::holds_alternative<engine::render::CmdDrawDecal>(cmd)) {
            found_decal = true;
            break;
        }
    }
    EXPECT_TRUE(found_decal);
}
