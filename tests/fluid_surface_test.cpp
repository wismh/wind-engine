#include <gtest/gtest.h>

#include <engine/builtin_ids.h>
#include <engine/core/bound_windows.h>
#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/fluid_render_2d.h>
#include <engine/render/graphics.h>
#include <engine/render/particles.h>
#include <engine/render/primitives.h>
#include <engine/resources/assets_db.h>
#include <engine/ui/presentation.h>

#include "fixtures/cooked_shaders.h"
#include "fixtures/fatal_fakes.h"
#include "shader_cook/cook.h"

#include <glm/vec4.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

// LQ-54: FluidRender2D's thresholded surface (CmdDrawFluidSurface).

namespace {

class TagShader final : public engine::render::IShader {};

class TagTexture final : public engine::render::ITexture {};

class FakeMesh final : public engine::render::IMesh {
public:
    FakeMesh() : engine::render::IMesh(engine::render::make_cube(1.0f)) {}
    explicit FakeMesh(const engine::render::MeshDesc& desc) : engine::render::IMesh(desc) {}

protected:
    bool replace(const engine::render::MeshDesc&) override { return true; }
};

class AssetFactory final : public engine::render::IGraphicFactory {
public:
    std::shared_ptr<engine::render::IMesh> create_mesh(const engine::render::MeshDesc& desc) override {
        return std::make_shared<FakeMesh>(desc);
    }
    std::shared_ptr<engine::render::IShader> create_shader(const engine::render::ShaderDesc&) override {
        return std::make_shared<TagShader>();
    }
    std::shared_ptr<engine::render::ITexture> create_texture(const engine::render::TextureDesc&) override {
        ++textures;
        return std::make_shared<TagTexture>();
    }
    std::shared_ptr<engine::render::ITexture> create_texture_array(const engine::render::TextureArrayDesc&) override {
        return {};
    }
    std::shared_ptr<engine::render::IDynamicTexture> create_dynamic_texture(
            const engine::render::DynamicTextureDesc&) override {
        return {};
    }

    int textures = 0;
};

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
}

// Cooked stand-ins for the builtin shaders the fluid look reads; `with_surface` false leaves the composite out.
class SurfaceAssets {
public:
    explicit SurfaceAssets(bool with_surface) : assets_(fatal_) {
        engine::shader_cook::register_shader_cooker();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("wind_fluid_surface_" + std::to_string(stamp));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        engine::CookedCatalog catalog;
        const auto add_shader = [&](engine::AssetId id, const std::string& name) {
            write_text(root_ / name, "<xml><vertex>void main(){}</vertex><fragment>void main(){}</fragment></xml>");
            EXPECT_TRUE(cook_shader_in_place(root_ / name));
            catalog.add(engine::CatalogEntry{
                    .guid = id, .relative_path = name, .importer = engine::ImporterKind::Shader});
        };
        add_shader(engine::builtin::shader_unlit, "unlit.shader");
        add_shader(engine::builtin::shader_post_grade, "grade.shader");
        if (with_surface) {
            add_shader(engine::builtin::shader_fluid_surface, "fluid_surface.shader");
        }
        assets_.set_graphic_factory(&factory_);
        assets_.set_catalog(std::move(catalog));
        assets_.set_root(root_);
    }

    ~SurfaceAssets() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    SurfaceAssets(const SurfaceAssets&) = delete;
    SurfaceAssets& operator=(const SurfaceAssets&) = delete;

    engine::AssetsDb& db() { return assets_; }
    fakes::QuietFatal& fatal() { return fatal_; }

    std::shared_ptr<engine::render::IShader> shader(engine::AssetId id) {
        auto found = assets_.try_get<engine::render::IShader>(id);
        return found.has_value() ? *found : nullptr;
    }

private:
    fakes::QuietFatal fatal_;
    AssetFactory factory_;
    engine::AssetsDb assets_;
    std::filesystem::path root_;
};

void spawn_camera(engine::ecs::World& world) {
    const engine::ecs::Entity camera = world.create();
    world.emplace<engine::Transform>(camera, engine::Transform{.position = {0.0f, 0.0f, 10.0f}});
    engine::Camera& lens = world.emplace<engine::Camera>(camera, engine::Camera{});
    lens.projection = engine::Projection::Perspective;
    lens.near_clip = 0.1f;
    lens.far_clip = 100.0f;
    world.ctx<engine::ActiveCamera>().entity = camera;
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};
}

engine::ecs::Entity spawn_water(engine::ecs::World& world, engine::render::FluidRender2D look) {
    const engine::ecs::Entity entity = world.create();
    engine::Fluid2D& fluid = world.emplace<engine::Fluid2D>(entity, engine::Fluid2DParams{.radius = 0.05f});
    (void)fluid.spawn({0.0f, 0.0f}, {0.0f, 0.0f});
    (void)fluid.spawn({0.1f, 0.0f}, {1.0f, 0.0f});
    (void)fluid.spawn({0.2f, 0.0f}, {0.0f, -4.0f});
    look.mesh = std::make_shared<FakeMesh>();
    world.emplace<engine::render::FluidRender2D>(entity, std::move(look));
    return entity;
}

template<class T>
const T* first_of(const engine::render::CommandBuffer& commands) {
    for (const engine::render::Command& command : commands) {
        if (const auto* found = std::get_if<T>(&command)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

TEST(FluidSurface, DefaultLookIsASurfaceOfAdditiveDensitySplatsOverAnOffscreenScene) {
    SurfaceAssets assets{true};
    engine::ecs::World world;
    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{
                                                   .commands = &commands,
                                                   .fatal = &assets.fatal(),
                                                   .assets = &assets.db(),
                                           });
    spawn_camera(world);
    (void)spawn_water(world, engine::render::FluidRender2D{
                                     .color = {0.1f, 0.4f, 0.9f, 0.7f},
                                     .foam_color = {1.0f, 1.0f, 1.0f, 1.0f},
                                     .foam_speed = 2.0f,
                                     .splat_weight = 0.5f,
                                     .threshold = 0.2f,
                                     .edge = 0.05f,
                                     .refraction = 4.0f,
                                     .foam_scale = 1.5f,
                             });
    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(first_of<engine::render::CmdDrawParticles>(commands), nullptr) << "no cards beside the surface";
    const auto* surface = first_of<engine::render::CmdDrawFluidSurface>(commands);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->composite, assets.shader(engine::builtin::shader_fluid_surface));
    EXPECT_EQ(surface->color, (glm::vec4{0.1f, 0.4f, 0.9f, 0.7f}));
    EXPECT_EQ(surface->params, (glm::vec4{0.2f, 0.05f, 4.0f, 1.5f}));
    const engine::render::CmdDrawParticles& splats = surface->splats;
    EXPECT_EQ(splats.blend, engine::render::BlendMode::Additive);
    EXPECT_EQ(splats.draw, engine::render::ParticleDraw::Plane);
    ASSERT_NE(splats.material, nullptr);
    EXPECT_EQ(splats.material->shader(), assets.shader(engine::builtin::shader_unlit));
    EXPECT_NE(splats.material->texture(0), nullptr) << "the generated soft disc";
    ASSERT_EQ(splats.instances.size(), 3u);
    EXPECT_EQ(splats.instances[0].color, (glm::vec4{0.5f, 0.0f, 0.0f, 1.0f})) << "at rest: density only";
    EXPECT_EQ(splats.instances[1].color, (glm::vec4{0.5f, 0.25f, 0.0f, 1.0f})) << "half the foam speed";
    EXPECT_EQ(splats.instances[2].color, (glm::vec4{0.5f, 0.5f, 0.0f, 1.0f})) << "at or past it: all foam";

    // The surface reads the scene behind it, so the scene renders offscreen, with a copy shader for Vulkan.
    const auto* view = first_of<engine::render::CmdSetView>(commands);
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(view->post.active());
    EXPECT_EQ(view->post.ldr_copy, assets.shader(engine::builtin::shader_post_grade));
    EXPECT_NE(first_of<engine::render::CmdPostProcess>(commands), nullptr);
}

TEST(FluidSurface, CardsLookDrawsCardsAndLeavesTheSceneOnscreen) {
    SurfaceAssets assets{true};
    engine::ecs::World world;
    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{
                                                   .commands = &commands,
                                                   .fatal = &assets.fatal(),
                                                   .assets = &assets.db(),
                                           });
    spawn_camera(world);
    (void)spawn_water(world, engine::render::FluidRender2D{.look = engine::render::FluidRender2D::Look::Cards});
    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(first_of<engine::render::CmdDrawFluidSurface>(commands), nullptr);
    const auto* cards = first_of<engine::render::CmdDrawParticles>(commands);
    ASSERT_NE(cards, nullptr);
    EXPECT_EQ(cards->blend, engine::render::BlendMode::Alpha);
    const auto* view = first_of<engine::render::CmdSetView>(commands);
    ASSERT_NE(view, nullptr);
    EXPECT_FALSE(view->post.active());
}

TEST(FluidSurface, WithoutTheCompositeShaderTheSurfaceFallsBackToCards) {
    SurfaceAssets assets{false};
    engine::ecs::World world;
    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{
                                                   .commands = &commands,
                                                   .fatal = &assets.fatal(),
                                                   .assets = &assets.db(),
                                           });
    spawn_camera(world);
    (void)spawn_water(world, engine::render::FluidRender2D{.color = {0.0f, 0.0f, 1.0f, 1.0f}});
    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(first_of<engine::render::CmdDrawFluidSurface>(commands), nullptr);
    const auto* cards = first_of<engine::render::CmdDrawParticles>(commands);
    ASSERT_NE(cards, nullptr);
    ASSERT_EQ(cards->instances.size(), 3u);
    EXPECT_EQ(cards->instances[0].color, (glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}));
}
