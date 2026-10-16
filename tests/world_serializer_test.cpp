#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/pointer_target.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world_asset.h>
#include <engine/ecs/world_io.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/ambient_light.h>
#include <engine/render/animation.h>
#include <engine/render/directional_light.h>
#include <engine/render/renderable.h>
#include <engine/render/sprite.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class RecordingFatalError final : public engine::IFatalError {
public:
    [[noreturn]] void report(std::string_view message) override {
        last_message = std::string(message);
        std::abort();
    }

    std::string last_message;
};

enum class EnemyAiState : int {
    Idle = 0,
    Patrol = 1,
    Chase = 2,
};

struct WIND_COMPONENT EnemySpawner {
    float interval = 2.5f;
    int max_count = 5;
    bool active = true;
    std::string tag = "default_wave";
    glm::vec2 offset{1.0f, 2.0f};
    engine::AssetId enemy_sprite{};
    engine::ecs::Entity patrol_target{};
    EnemyAiState initial_state = EnemyAiState::Idle;
    std::optional<float> override_speed{};
    std::vector<int> wave_sizes{1, 2, 3};
};

void register_test_spawner(engine::ecs::WorldSerializer& serializer) {
    serializer.register_component<EnemySpawner>(
            "enemy_spawner",
            [](engine::ecs::ComponentWriter& out, const EnemySpawner& c) {
                out.field("interval", c.interval);
                out.field("max_count", c.max_count);
                out.field("active", c.active);
                out.field("tag", c.tag);
                out.field("offset", c.offset);
                out.field("enemy_sprite", c.enemy_sprite);
                out.field("patrol_target", c.patrol_target);
                out.field("initial_state", c.initial_state);
                out.field("override_speed", c.override_speed);
                out.field("wave_sizes", c.wave_sizes);
            },
            [](engine::ecs::ComponentReader& in, EnemySpawner& c) {
                (void)in.field("interval", c.interval);
                (void)in.field("max_count", c.max_count);
                (void)in.field("active", c.active);
                (void)in.field("tag", c.tag);
                (void)in.field("offset", c.offset);
                (void)in.field("enemy_sprite", c.enemy_sprite);
                (void)in.field("patrol_target", c.patrol_target);
                (void)in.field("initial_state", c.initial_state);
                (void)in.field("override_speed", c.override_speed);
                (void)in.field("wave_sizes", c.wave_sizes);
            });
}

} // namespace

TEST(WorldSerializer, ParsesAndInstantiatesBuiltinComponentsAndHierarchy) {
    constexpr std::string_view kToml = R"(
version = 1
active_camera = "main_cam"

[[entities]]
id = "main_cam"
transform = { position = [0.0, 0.0, 10.0] }
camera = { projection = "perspective", fov_y = 1.0, near_clip = 0.1, far_clip = 500.0 }

[[entities]]
id = "sun"
transform = { euler_xyz = [0.1, 0.2, 0.3] }
directional_light = { color = [1.0, 0.9, 0.8], intensity = 1.5 }
ambient_light = { color = [0.2, 0.3, 0.4], intensity = 0.25 }

[[entities]]
id = "player"
transform = { position = [3.0, 4.0, 0.0], rotation_z = 0.0, scale = [2.0, 2.0, 1.0] }
rigid_body_2d = { velocity = [1.5, -0.5, 0.0] }
box_collider_2d = { size = [2.0, 3.0, 1.0], layer = 2, mask = 6, is_trigger = true }
pointer_target = { size = [2.0, 3.0], pivot = [0.5, 0.0], layer = 1, order_in_layer = 5, enabled = true }

[[entities]]
id = "shield"
parent = "player"
transform = { position = [1.0, 0.0, 0.0] }
circle_collider_2d = { radius = 0.75, layer = 4, mask = 1, is_trigger = false }
)";

    engine::ecs::World world;
    auto loaded = engine::ecs::load_world_from_toml(world, kToml);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_EQ(loaded->entities.size(), 4u);

    const engine::ecs::Entity cam_entity = loaded->by_id.at("main_cam");
    const engine::ecs::Entity sun_entity = loaded->by_id.at("sun");
    const engine::ecs::Entity player_entity = loaded->by_id.at("player");
    const engine::ecs::Entity shield_entity = loaded->by_id.at("shield");

    EXPECT_EQ(world.ctx<engine::ActiveCamera>().entity, cam_entity);
    const auto* cam = world.try_get<engine::Camera>(cam_entity);
    ASSERT_NE(cam, nullptr);
    EXPECT_EQ(cam->projection, engine::Projection::Perspective);
    EXPECT_FLOAT_EQ(cam->fov_y, 1.0f);
    EXPECT_FLOAT_EQ(cam->near_clip, 0.1f);
    EXPECT_FLOAT_EQ(cam->far_clip, 500.0f);

    const auto* dir_light = world.try_get<engine::render::DirectionalLight>(sun_entity);
    ASSERT_NE(dir_light, nullptr);
    EXPECT_FLOAT_EQ(dir_light->intensity, 1.5f);
    const auto* amb_light = world.try_get<engine::render::AmbientLight>(sun_entity);
    ASSERT_NE(amb_light, nullptr);
    EXPECT_FLOAT_EQ(amb_light->intensity, 0.25f);

    const auto* rb = world.try_get<engine::RigidBody2D>(player_entity);
    ASSERT_NE(rb, nullptr);
    EXPECT_FLOAT_EQ(rb->velocity.x, 1.5f);
    EXPECT_FLOAT_EQ(rb->velocity.y, -0.5f);

    const auto* box = world.try_get<engine::BoxCollider2D>(player_entity);
    ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->size.x, 2.0f);
    EXPECT_FLOAT_EQ(box->size.y, 3.0f);
    EXPECT_EQ(box->layer, 2u);
    EXPECT_EQ(box->mask, 6u);
    EXPECT_TRUE(box->is_trigger);

    const auto* pt = world.try_get<engine::PointerTarget>(player_entity);
    ASSERT_NE(pt, nullptr);
    EXPECT_FLOAT_EQ(pt->pivot.y, 0.0f);
    EXPECT_EQ(pt->order_in_layer, 5);

    const auto* parent = world.try_get<engine::Parent>(shield_entity);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(parent->entity, player_entity);
    const glm::vec3 shield_world_pos = engine::ecs::world_position(world, shield_entity);
    // Player at (3, 4, 0) with scale (2, 2, 1) and child at local (1, 0, 0) -> world (5, 4, 0).
    EXPECT_FLOAT_EQ(shield_world_pos.x, 5.0f);
    EXPECT_FLOAT_EQ(shield_world_pos.y, 4.0f);

    const auto* circle = world.try_get<engine::CircleCollider2D>(shield_entity);
    ASSERT_NE(circle, nullptr);
    EXPECT_FLOAT_EQ(circle->radius, 0.75f);
}

TEST(WorldSerializer, GameSpecificComponentRoundTripsAndPreservesCppDefaults) {
    constexpr std::string_view kToml = R"(
version = 1

[[entities]]
id = "waypoint_a"
transform = { position = [10.0, 20.0, 0.0] }

[[entities]]
id = "spawner_1"
enemy_spawner = { interval = 0.75, enemy_sprite = "0123456789abcdef0123456789abcdef", patrol_target = "waypoint_a", initial_state = 2, override_speed = 9.5, wave_sizes = [4, 8] }
)";

    engine::ecs::World world;
    register_test_spawner(world.ctx<engine::ecs::WorldSerializer>());

    auto loaded = engine::ecs::load_world_from_toml(world, kToml);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    const engine::ecs::Entity waypoint = loaded->by_id.at("waypoint_a");
    const engine::ecs::Entity spawner_entity = loaded->by_id.at("spawner_1");
    const auto* spawner = world.try_get<EnemySpawner>(spawner_entity);
    ASSERT_NE(spawner, nullptr);

    EXPECT_FLOAT_EQ(spawner->interval, 0.75f);
    EXPECT_EQ(spawner->max_count, 5) << "omitted field keeps C++ default initializer";
    EXPECT_TRUE(spawner->active);
    EXPECT_EQ(spawner->tag, "default_wave");
    EXPECT_EQ(spawner->enemy_sprite, engine::AssetId{"0123456789abcdef0123456789abcdef"});
    EXPECT_EQ(spawner->patrol_target, waypoint);
    EXPECT_EQ(spawner->initial_state, EnemyAiState::Chase);
    ASSERT_TRUE(spawner->override_speed.has_value());
    EXPECT_FLOAT_EQ(*spawner->override_speed, 9.5f);
    EXPECT_EQ(spawner->wave_sizes, (std::vector<int>{4, 8}));

    // Round-trip save and reload into a fresh world.
    const std::string saved_toml = engine::ecs::save_world_to_toml(world);

    engine::ecs::World world2;
    register_test_spawner(world2.ctx<engine::ecs::WorldSerializer>());
    auto reloaded = engine::ecs::load_world_from_toml(world2, saved_toml);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;

    const engine::ecs::Entity waypoint2 = reloaded->by_id.at("waypoint_a");
    const engine::ecs::Entity spawner2_entity = reloaded->by_id.at("spawner_1");
    const auto* spawner2 = world2.try_get<EnemySpawner>(spawner2_entity);
    ASSERT_NE(spawner2, nullptr);
    EXPECT_FLOAT_EQ(spawner2->interval, 0.75f);
    EXPECT_EQ(spawner2->max_count, 5);
    EXPECT_EQ(spawner2->enemy_sprite, engine::AssetId{"0123456789abcdef0123456789abcdef"});
    EXPECT_EQ(spawner2->patrol_target, waypoint2);
    EXPECT_EQ(spawner2->initial_state, EnemyAiState::Chase);
    ASSERT_TRUE(spawner2->override_speed.has_value());
    EXPECT_FLOAT_EQ(*spawner2->override_speed, 9.5f);
    EXPECT_EQ(spawner2->wave_sizes, (std::vector<int>{4, 8}));
}

TEST(WorldSerializer, RejectsInvalidWorldFilesAndRollsBackOnComponentError) {
    {
        const auto res = engine::ecs::parse_world_asset("not valid toml ===");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, engine::ecs::WorldErrorKind::InvalidToml);
    }
    {
        const auto res = engine::ecs::parse_world_asset("version = 99\n");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, engine::ecs::WorldErrorKind::UnsupportedVersion);
    }
    {
        const auto res = engine::ecs::parse_world_asset(R"(
[[entities]]
id = "dup"
[[entities]]
id = "dup"
)");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, engine::ecs::WorldErrorKind::DuplicateEntityId);
    }
    {
        const auto res = engine::ecs::parse_world_asset(R"(
[[entities]]
id = "child"
parent = "missing_parent"
)");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, engine::ecs::WorldErrorKind::UnknownParent);
    }
    {
        const auto res = engine::ecs::parse_world_asset(R"(
[[entities]]
id = "a"
parent = "b"
[[entities]]
id = "b"
parent = "a"
)");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, engine::ecs::WorldErrorKind::HierarchyCycle);
    }
    {
        const auto res = engine::ecs::parse_world_asset(R"(
active_camera = "no_such_cam"
[[entities]]
id = "a"
)");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, engine::ecs::WorldErrorKind::UnknownActiveCamera);
    }
    {
        engine::ecs::World world;
        const auto res = engine::ecs::load_world_from_toml(world, R"(
[[entities]]
id = "a"
unregistered_component = { x = 1 }
)");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, engine::ecs::WorldErrorKind::UnknownComponent);
        EXPECT_TRUE(world.alive_entities().empty());
    }
    {
        engine::ecs::World world;
        const auto res = engine::ecs::load_world_from_toml(world, R"(
[[entities]]
id = "a"
transform = { position = "wrong_type" }
)");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, engine::ecs::WorldErrorKind::InvalidField);
        EXPECT_TRUE(world.alive_entities().empty()) << "failed load rolls back created entities";
    }
}

TEST(WorldSerializer, LoadsWorldAssetThroughAssetsDb) {
    const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "wind_world_asset_db_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const std::filesystem::path world_file = dir / "stage.world";
    {
        std::ofstream out(world_file, std::ios::binary);
        out << R"(
version = 1
[[entities]]
id = "hero"
transform = { position = [7.0, 8.0, 0.0] }
)";
    }

    RecordingFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(dir);

    constexpr engine::AssetId kStageId{"0123456789abcdef0123456789abcdef"};
    engine::CookedCatalog catalog;
    catalog.add(engine::CatalogEntry{
            .guid = kStageId,
            .relative_path = "stage.world",
            .importer = engine::ImporterKind::World,
    });
    db.set_catalog(std::move(catalog));

    const auto world_asset = db.try_get<engine::ecs::WorldAsset>(kStageId);
    ASSERT_TRUE(world_asset.has_value());
    ASSERT_EQ((*world_asset)->entities.size(), 1u);
    EXPECT_EQ((*world_asset)->entities[0].id, "hero");

    engine::ecs::World world;
    const auto inst = engine::ecs::instantiate_world(world, **world_asset, &db);
    ASSERT_TRUE(inst.has_value());
    const auto* tr = world.try_get<engine::Transform>(inst->by_id.at("hero"));
    ASSERT_NE(tr, nullptr);
    EXPECT_FLOAT_EQ(tr->position.x, 7.0f);
    EXPECT_FLOAT_EQ(tr->position.y, 8.0f);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
