#include <gtest/gtest.h>

#include <engine/builtin_ids.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>
#include <engine/render/sprite.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/meta.h>
#include <engine/ui/canvas.h>

#include <glm/vec4.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class FakeMesh final : public engine::render::IMesh {};
class FakeTexture final : public engine::render::ITexture {};
class FakeShader final : public engine::render::IShader {};

class FakeMaterial final : public engine::render::IMaterial {
public:
    explicit FakeMaterial(engine::render::BlendMode blend = engine::render::BlendMode::Alpha)
        : blend_(blend) {}

    std::shared_ptr<engine::render::IShader> shader() const override {
        return {};
    }

    std::shared_ptr<engine::render::ITexture> texture(int) const override {
        return {};
    }

    glm::vec4 color() const override {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    engine::render::BlendMode blend() const override {
        return blend_;
    }

private:
    engine::render::BlendMode blend_;
};

class FakeGraphicFactory final : public engine::render::IGraphicFactory {
public:
    std::shared_ptr<engine::render::IMesh> create_mesh(const engine::render::MeshDesc&) override {
        return std::make_shared<FakeMesh>();
    }

    std::shared_ptr<engine::render::IShader> create_shader(const engine::render::ShaderDesc&) override {
        return std::make_shared<FakeShader>();
    }

    std::shared_ptr<engine::render::ITexture> create_texture(const engine::render::TextureDesc&) override {
        return std::make_shared<FakeTexture>();
    }
};

class RecordingFatalError final : public engine::IFatalError {
public:
    int call_count = 0;
    std::string last_message;

    void report(std::string_view message) override {
        ++call_count;
        last_message = std::string(message);
        throw std::runtime_error(last_message);
    }
};

struct TempTree {
    std::filesystem::path path;

    TempTree() {
        static int seq = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
                ("wind_sprite_test_" + std::to_string(stamp) + "_" + std::to_string(++seq));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;
};

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
}

void spawn_camera(engine::ecs::World& world) {
    const engine::ecs::Entity camera = world.create();
    world.emplace<engine::Transform>(camera, engine::Transform{});
    world.emplace<engine::Camera>(camera, engine::Camera{});
    world.ctx<engine::ActiveCamera>().entity = camera;
    world.ctx<engine::ui::WindowSize>().width = 800;
    world.ctx<engine::ui::WindowSize>().height = 600;
}

TEST(Sprite, MaterialClassConstructibleInCpp) {
    const auto shader = std::make_shared<FakeShader>();
    const auto texture = std::make_shared<FakeTexture>();
    const glm::vec4 color{0.5f, 0.6f, 0.7f, 1.0f};

    engine::render::Material mat(shader, texture, color, engine::render::BlendMode::Additive);
    EXPECT_EQ(mat.shader(), shader);
    EXPECT_EQ(mat.texture(0), texture);
    EXPECT_EQ(mat.texture(1), nullptr);
    EXPECT_EQ(mat.color(), color);
    EXPECT_EQ(mat.blend(), engine::render::BlendMode::Additive);
}

TEST(Sprite, TintedColorMultipliesSpriteAndMaterial) {
    const auto mat = std::make_shared<engine::render::Material>(
            nullptr, nullptr, glm::vec4{1.0f, 0.5f, 0.0f, 1.0f});
    engine::render::Sprite sprite{
            .texture = nullptr,
            .color = glm::vec4{0.5f, 0.5f, 1.0f, 0.8f},
            .material = mat,
    };
    const glm::vec4 tinted = sprite.tinted_color();
    EXPECT_FLOAT_EQ(tinted.r, 0.5f);
    EXPECT_FLOAT_EQ(tinted.g, 0.25f);
    EXPECT_FLOAT_EQ(tinted.b, 0.0f);
    EXPECT_FLOAT_EQ(tinted.a, 0.8f);
}

TEST(Sprite, CustomMeshAndMaterialEmittedInCommands) {
    engine::ecs::World world;
    spawn_camera(world);

    const auto custom_mesh = std::make_shared<FakeMesh>();
    const auto custom_mat = std::make_shared<FakeMaterial>();
    const auto texture = std::make_shared<FakeTexture>();

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = {1.0f, 2.0f, 0.0f}});
    world.emplace<engine::render::Sprite>(entity, engine::render::Sprite{
            .texture = texture,
            .color = glm::vec4{0.2f, 0.4f, 0.6f, 1.0f},
            .layer = 2,
            .order_in_layer = 5,
            .mesh = custom_mesh,
            .material = custom_mat,
    });

    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 1u);
    const auto* cmd = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[0]);
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->mesh, custom_mesh);
    EXPECT_EQ(cmd->material, custom_mat);
    EXPECT_EQ(cmd->color, (glm::vec4{0.2f, 0.4f, 0.6f, 1.0f}));
    EXPECT_FLOAT_EQ(cmd->model[3][0], 1.0f);
    EXPECT_FLOAT_EQ(cmd->model[3][1], 2.0f);
}

TEST(Sprite, FlipXAndFlipYNegateModelScale) {
    engine::ecs::World world;
    spawn_camera(world);

    const auto mesh = std::make_shared<FakeMesh>();
    const auto mat = std::make_shared<FakeMaterial>();

    const engine::ecs::Entity e_normal = world.create();
    world.emplace<engine::Transform>(e_normal, engine::Transform{.scale = {2.0f, 3.0f, 1.0f}});
    world.emplace<engine::render::Sprite>(e_normal, engine::render::Sprite{
            .flip_x = false,
            .flip_y = false,
            .mesh = mesh,
            .material = mat,
    });

    const engine::ecs::Entity e_flipped = world.create();
    world.emplace<engine::Transform>(e_flipped, engine::Transform{.scale = {2.0f, 3.0f, 1.0f}});
    world.emplace<engine::render::Sprite>(e_flipped, engine::render::Sprite{
            .flip_x = true,
            .flip_y = true,
            .mesh = mesh,
            .material = mat,
    });

    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 2u);
    const auto* cmd_normal = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[0]);
    const auto* cmd_flipped = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[1]);
    ASSERT_NE(cmd_normal, nullptr);
    ASSERT_NE(cmd_flipped, nullptr);

    // Normal scale: X = 2.0, Y = 3.0
    EXPECT_FLOAT_EQ(cmd_normal->model[0][0], 2.0f);
    EXPECT_FLOAT_EQ(cmd_normal->model[1][1], 3.0f);

    // Flipped scale: X = -2.0, Y = -3.0
    EXPECT_FLOAT_EQ(cmd_flipped->model[0][0], -2.0f);
    EXPECT_FLOAT_EQ(cmd_flipped->model[1][1], -3.0f);
}

TEST(Sprite, MissingMeshOrMaterialWithoutAssetsReportsFatal) {
    engine::ecs::World world;
    spawn_camera(world);

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    // Sprite with null mesh and null material, and deps.assets is null -> reports fatal
    world.emplace<engine::render::Sprite>(entity, engine::render::Sprite{});

    engine::render::CommandBuffer commands;
    RecordingFatalError fatal;
    engine::register_engine_systems(world, engine::EngineSystemDeps{
            .commands = &commands,
            .fatal = &fatal,
    });

    EXPECT_THROW(world.run(engine::ecs::Schedule::Frame), std::runtime_error);
    EXPECT_EQ(fatal.call_count, 1);
    EXPECT_EQ(fatal.last_message, "Sprite is missing mesh or material");
}

TEST(Sprite, InterleavedSortingWithRenderable) {
    engine::ecs::World world;
    spawn_camera(world);

    const auto mesh = std::make_shared<FakeMesh>();
    const auto mat_a = std::make_shared<FakeMaterial>();
    const auto mat_b = std::make_shared<FakeMaterial>();

    // Entity 1: Layer 1, Sprite with mat_a
    const engine::ecs::Entity e1 = world.create();
    world.emplace<engine::Transform>(e1, engine::Transform{});
    world.emplace<engine::render::Sprite>(e1, engine::render::Sprite{
            .layer = 1,
            .order_in_layer = 0,
            .mesh = mesh,
            .material = mat_a,
    });

    // Entity 2: Layer 0, Renderable with mat_b
    const engine::ecs::Entity e2 = world.create();
    world.emplace<engine::Transform>(e2, engine::Transform{});
    world.emplace<engine::render::Renderable>(e2, engine::render::Renderable{
            .mesh = mesh,
            .material = mat_b,
            .layer = 0,
            .order_in_layer = 0,
    });

    // Entity 3: Layer 1, Sprite with mat_a, higher order_in_layer
    const engine::ecs::Entity e3 = world.create();
    world.emplace<engine::Transform>(e3, engine::Transform{});
    world.emplace<engine::render::Sprite>(e3, engine::render::Sprite{
            .layer = 1,
            .order_in_layer = 1,
            .mesh = mesh,
            .material = mat_a,
    });

    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 3u);
    // Command 0 MUST be layer 0 (Renderable e2)
    const auto* cmd0 = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[0]);
    ASSERT_NE(cmd0, nullptr);
    EXPECT_EQ(cmd0->material, mat_b);

    // Commands 1 and 2 must be layer 1 (Sprites e1 and e3)
    const auto* cmd1 = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[1]);
    const auto* cmd2 = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[2]);
    ASSERT_NE(cmd1, nullptr);
    ASSERT_NE(cmd2, nullptr);
    EXPECT_EQ(cmd1->material, mat_a);
    EXPECT_EQ(cmd2->material, mat_a);
}

TEST(Sprite, AutomaticMeshAndMaterialResolutionWithAssets) {
    TempTree tree;
    write_file(tree.path / "quad.mesh", "-0.5 0.5 0 0 1\n");
    write_file(tree.path / "unlit.shader",
            "<xml><vertex>void main(){}</vertex><fragment>void main(){}</fragment></xml>");

    engine::CookedCatalog catalog;
    catalog.add(engine::CatalogEntry{
            .guid = engine::builtin::mesh_quad,
            .relative_path = "quad.mesh",
            .importer = engine::ImporterKind::Mesh,
    });
    catalog.add(engine::CatalogEntry{
            .guid = engine::builtin::shader_unlit,
            .relative_path = "unlit.shader",
            .importer = engine::ImporterKind::Shader,
    });

    FakeGraphicFactory factory;
    RecordingFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_catalog(std::move(catalog));
    db.set_root(tree.path);
    db.set_graphic_factory(&factory);

    engine::ecs::World world;
    spawn_camera(world);

    const auto tex_a = std::make_shared<FakeTexture>();
    const auto tex_b = std::make_shared<FakeTexture>();

    // Sprite 1 with tex_a
    const engine::ecs::Entity e1 = world.create();
    world.emplace<engine::Transform>(e1, engine::Transform{});
    world.emplace<engine::render::Sprite>(e1, engine::render::Sprite{.texture = tex_a, .order_in_layer = 0});

    // Sprite 2 with tex_a (should share material with e1!)
    const engine::ecs::Entity e2 = world.create();
    world.emplace<engine::Transform>(e2, engine::Transform{});
    world.emplace<engine::render::Sprite>(e2, engine::render::Sprite{.texture = tex_a, .order_in_layer = 1});

    // Sprite 3 with tex_b (different texture -> different material!)
    const engine::ecs::Entity e3 = world.create();
    world.emplace<engine::Transform>(e3, engine::Transform{});
    world.emplace<engine::render::Sprite>(e3, engine::render::Sprite{.texture = tex_b, .order_in_layer = 2});

    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{
            .commands = &commands,
            .assets = &db,
    });
    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 3u);
    const auto* cmd1 = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[0]);
    const auto* cmd2 = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[1]);
    const auto* cmd3 = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[2]);
    ASSERT_NE(cmd1, nullptr);
    ASSERT_NE(cmd2, nullptr);
    ASSERT_NE(cmd3, nullptr);

    // All sprites automatically resolved the quad mesh
    ASSERT_NE(cmd1->mesh, nullptr);
    EXPECT_EQ(cmd1->mesh, cmd2->mesh);
    EXPECT_EQ(cmd1->mesh, cmd3->mesh);

    // Sprites with same texture (e1 & e2) share the exact same Material instance!
    ASSERT_NE(cmd1->material, nullptr);
    EXPECT_EQ(cmd1->material, cmd2->material);
    EXPECT_EQ(cmd1->material->texture(0), tex_a);

    // Sprite with different texture (e3) has a distinct Material instance pointing to tex_b!
    ASSERT_NE(cmd3->material, nullptr);
    EXPECT_NE(cmd1->material, cmd3->material);
    EXPECT_EQ(cmd3->material->texture(0), tex_b);
}

}
