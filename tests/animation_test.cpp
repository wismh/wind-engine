#include <gtest/gtest.h>

#include <engine/builtin_ids.h>
#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/animation.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/sprite.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/meta.h>
#include <engine/ui/canvas.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace {

constexpr std::uint8_t kPng1x1Red[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
        0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x08,
        0x3E, 0x33, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

class FakeTexture final : public engine::render::ITexture {
public:
    int w = 0;
    int h = 0;
    explicit FakeTexture(int width = 0, int height = 0) : w(width), h(height) {}
    int width() const noexcept override { return w; }
    int height() const noexcept override { return h; }
};

class FakeMesh final : public engine::render::IMesh {};
class FakeShader final : public engine::render::IShader {};

class FakeMaterial final : public engine::render::IMaterial {
public:
    std::shared_ptr<engine::render::IShader> shader() const override { return {}; }
    std::shared_ptr<engine::render::ITexture> texture(int) const override { return {}; }
    glm::vec4 color() const override { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    engine::render::BlendMode blend() const override { return engine::render::BlendMode::Alpha; }
};

class FakeGraphicFactory final : public engine::render::IGraphicFactory {
public:
    std::shared_ptr<engine::render::IMesh> create_mesh(const engine::render::MeshDesc&) override {
        return std::make_shared<FakeMesh>();
    }
    std::shared_ptr<engine::render::IShader> create_shader(const engine::render::ShaderDesc&) override {
        return std::make_shared<FakeShader>();
    }
    std::shared_ptr<engine::render::ITexture> create_texture(const engine::render::TextureDesc& desc) override {
        return std::make_shared<FakeTexture>(desc.width, desc.height);
    }
};

class SilentFatalError final : public engine::IFatalError {
public:
    void report(std::string_view) override {}
};

struct TempTree {
    std::filesystem::path path;

    TempTree() {
        static int seq = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
                ("wind_anim_" + std::to_string(stamp) + "_" + std::to_string(++seq));
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

void write_bytes(const std::filesystem::path& path, const std::uint8_t* data, std::size_t size) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

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
    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
}

}

TEST(Animation, ParseAnimationToml) {
    constexpr std::string_view kToml = R"(
fps = 12.0
loop = false

[[frames]]
texture = "a1b2c3d4e5f6789012345678901234ab"
sprite = "idle_0"
duration = 0.05

[[frames]]
texture = "b2c3d4e5f678901234567890123456cd"
)";

    const auto desc = engine::render::parse_animation(kToml);
    ASSERT_TRUE(desc.has_value());
    EXPECT_FLOAT_EQ(desc->fps, 12.0f);
    EXPECT_FALSE(desc->loop);
    ASSERT_EQ(desc->frames.size(), 2u);

    EXPECT_EQ(desc->frames[0].texture.hex(), "a1b2c3d4e5f6789012345678901234ab");
    EXPECT_EQ(desc->frames[0].sprite, "idle_0");
    ASSERT_TRUE(desc->frames[0].duration.has_value());
    EXPECT_FLOAT_EQ(*desc->frames[0].duration, 0.05f);

    EXPECT_EQ(desc->frames[1].texture.hex(), "b2c3d4e5f678901234567890123456cd");
    EXPECT_TRUE(desc->frames[1].sprite.empty());
    EXPECT_FALSE(desc->frames[1].duration.has_value());
}

TEST(Animation, ParseAnimationInvalidRejected) {
    // Missing frames:
    EXPECT_FALSE(engine::render::parse_animation("fps = 10.0\n").has_value());

    // Bad texture GUID:
    constexpr std::string_view kBadGuid = R"(
[[frames]]
texture = "not-a-hex"
)";
    EXPECT_FALSE(engine::render::parse_animation(kBadGuid).has_value());

    // Invalid fps:
    constexpr std::string_view kBadFps = R"(
fps = -5.0
[[frames]]
texture = "a1b2c3d4e5f6789012345678901234ab"
)";
    EXPECT_FALSE(engine::render::parse_animation(kBadFps).has_value());
}

TEST(Animation, LoadAnimationClipFromAssetsDb) {
    TempTree tree;
    write_bytes(tree.path / "sheet.png", kPng1x1Red, sizeof(kPng1x1Red));
    write_bytes(tree.path / "single.png", kPng1x1Red, sizeof(kPng1x1Red));

    const engine::AssetId sheet_id{"11111111111111111111111111111111"};
    const engine::AssetId single_id{"22222222222222222222222222222222"};
    const engine::AssetId anim_id{"33333333333333333333333333333333"};

    constexpr std::string_view kAnimToml = R"(
fps = 10.0
loop = true

[[frames]]
texture = "11111111111111111111111111111111"
sprite = "sub_0"

[[frames]]
texture = "22222222222222222222222222222222"
duration = 0.2
)";
    write_file(tree.path / "walk.anim", kAnimToml);

    engine::CookedCatalog catalog;
    {
        engine::CatalogEntry entry{sheet_id, "sheet.png", engine::ImporterKind::Texture};
        entry.texture.layout = engine::TextureLayout::Multiple;
        entry.texture.pixels_per_unit = 16.0f;
        entry.texture.sprites.push_back(engine::SpriteMeta{
                .name = "sub_0",
                .rect = {0, 0, 1, 1},
                .pivot = {0.5f, 0.0f},
        });
        catalog.add(std::move(entry));
    }
    {
        engine::CatalogEntry entry{single_id, "single.png", engine::ImporterKind::Texture};
        entry.texture.layout = engine::TextureLayout::Single;
        entry.texture.pixels_per_unit = 32.0f;
        catalog.add(std::move(entry));
    }
    {
        engine::CatalogEntry entry{anim_id, "walk.anim", engine::ImporterKind::Animation};
        catalog.add(std::move(entry));
    }

    FakeGraphicFactory factory;
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_catalog(std::move(catalog));
    db.set_root(tree.path);
    db.set_graphic_factory(&factory);

    auto clip = db.try_get<engine::render::SpriteAnimationClip>(anim_id);
    ASSERT_TRUE(clip.has_value());
    EXPECT_FLOAT_EQ((*clip)->fps, 10.0f);
    EXPECT_TRUE((*clip)->loop);
    ASSERT_EQ((*clip)->frames.size(), 2u);

    // Frame 0 from atlas:
    EXPECT_FLOAT_EQ((*clip)->frames[0].duration, 0.1f); // 1.0 / 10.0
    EXPECT_FLOAT_EQ((*clip)->frames[0].sprite.pixels_per_unit, 16.0f);
    EXPECT_FLOAT_EQ((*clip)->frames[0].sprite.pivot.y, 0.0f);

    // Frame 1 from single texture:
    EXPECT_FLOAT_EQ((*clip)->frames[1].duration, 0.2f); // override
    EXPECT_FLOAT_EQ((*clip)->frames[1].sprite.pixels_per_unit, 32.0f);

    EXPECT_FLOAT_EQ((*clip)->total_duration(), 0.3f);
}

TEST(Animation, SpriteAnimatorProgressionAndLooping) {
    auto clip = std::make_shared<engine::render::SpriteAnimationClip>();
    clip->fps = 10.0f;
    clip->loop = true;

    auto tex_a = std::make_shared<FakeTexture>(16, 16);
    auto tex_b = std::make_shared<FakeTexture>(32, 32);

    clip->frames.push_back(engine::render::SpriteAnimationFrame{
            .sprite = engine::render::Sprite{.texture = tex_a, .pixel_size = {16.0f, 16.0f}},
            .duration = 0.1f,
    });
    clip->frames.push_back(engine::render::SpriteAnimationFrame{
            .sprite = engine::render::Sprite{.texture = tex_b, .pixel_size = {32.0f, 32.0f}},
            .duration = 0.1f,
    });

    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 0.0f;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::render::Sprite>(entity, engine::render::Sprite{});
    world.emplace<engine::render::SpriteAnimator>(entity, engine::render::SpriteAnimator{
            .clip = clip,
            .elapsed = 0.0f,
            .current_frame = 0,
            .speed = 1.0f,
            .playing = true,
    });

    // Tick 1: delta_time = 0.05s -> still on frame 0
    world.ctx<engine::Time>().delta_time = 0.05f;
    engine::run_sprite_animations(world);

    auto& sprite = world.get<engine::render::Sprite>(entity);
    auto& anim = world.get<engine::render::SpriteAnimator>(entity);
    EXPECT_EQ(anim.current_frame, 0u);
    EXPECT_EQ(sprite.texture, tex_a);
    EXPECT_FLOAT_EQ(sprite.pixel_size.x, 16.0f);

    // Tick 2: delta_time = 0.06s (total elapsed = 0.11s) -> advances to frame 1
    world.ctx<engine::Time>().delta_time = 0.06f;
    engine::run_sprite_animations(world);
    EXPECT_EQ(anim.current_frame, 1u);
    EXPECT_EQ(sprite.texture, tex_b);
    EXPECT_FLOAT_EQ(sprite.pixel_size.x, 32.0f);

    // Tick 3: delta_time = 0.10s -> loops back to frame 0
    world.ctx<engine::Time>().delta_time = 0.10f;
    engine::run_sprite_animations(world);
    EXPECT_EQ(anim.current_frame, 0u);
    EXPECT_EQ(sprite.texture, tex_a);
}

TEST(Animation, NonLoopingAnimationStopsAtEnd) {
    auto clip = std::make_shared<engine::render::SpriteAnimationClip>();
    clip->fps = 10.0f;
    clip->loop = false;

    auto tex = std::make_shared<FakeTexture>(16, 16);
    clip->frames.push_back(engine::render::SpriteAnimationFrame{
            .sprite = engine::render::Sprite{.texture = tex},
            .duration = 0.1f,
    });
    clip->frames.push_back(engine::render::SpriteAnimationFrame{
            .sprite = engine::render::Sprite{.texture = tex},
            .duration = 0.1f,
    });

    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::render::Sprite>(entity, engine::render::Sprite{});
    world.emplace<engine::render::SpriteAnimator>(entity, engine::render::SpriteAnimator{
            .clip = clip,
            .playing = true,
    });

    // Jump past total duration:
    world.ctx<engine::Time>().delta_time = 0.5f;
    engine::run_sprite_animations(world);

    const auto& anim = world.get<engine::render::SpriteAnimator>(entity);
    EXPECT_EQ(anim.current_frame, 1u);
    EXPECT_FALSE(anim.playing);
}

TEST(Animation, FullFrameScheduleAppliesAndRendersAnimatedSprite) {
    engine::ecs::World world;
    spawn_camera(world);

    auto clip = std::make_shared<engine::render::SpriteAnimationClip>();
    clip->loop = true;
    auto tex = std::make_shared<FakeTexture>(32, 32);
    clip->frames.push_back(engine::render::SpriteAnimationFrame{
            .sprite = engine::render::Sprite{
                    .texture = tex,
                    .tiling = {0.5f, 0.5f},
                    .offset = {0.25f, 0.25f},
                    .pixel_size = {32.0f, 32.0f},
                    .pixels_per_unit = 16.0f, // world size = 2.0
            },
            .duration = 0.1f,
    });

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::render::Sprite>(entity, engine::render::Sprite{
            .mesh = std::make_shared<FakeMesh>(),
            .material = std::make_shared<FakeMaterial>(),
    });
    world.emplace<engine::render::SpriteAnimator>(entity, engine::render::SpriteAnimator{
            .clip = clip,
            .playing = true,
    });

    world.ctx<engine::Time>().delta_time = 0.05f;

    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});

    // Run frame schedule (includes Game phase -> run_sprite_animations, and Render phase -> run_render)
    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 1u);
    const auto* cmd = std::get_if<engine::render::CmdDrawMesh>(&commands.commands()[0]);
    ASSERT_NE(cmd, nullptr);
    EXPECT_FLOAT_EQ(cmd->uv_scale.x, 0.5f);
    EXPECT_FLOAT_EQ(cmd->uv_scale.y, 0.5f);
    EXPECT_FLOAT_EQ(cmd->uv_offset.x, 0.25f);
    EXPECT_FLOAT_EQ(cmd->uv_offset.y, 0.25f);
    // World size 32/16 = 2.0
    EXPECT_FLOAT_EQ(cmd->model[0][0], 2.0f);
    EXPECT_FLOAT_EQ(cmd->model[1][1], 2.0f);
}
