#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/prototype_asset.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>
#include <engine/ecs/world_serializer.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace {

using engine::AssetId;
using engine::ecs::Entity;
using engine::ecs::FieldArray;
using engine::ecs::FieldTable;
using engine::ecs::FieldValue;
using engine::ecs::PrototypeAsset;
using engine::ecs::PrototypeErrorKind;
using engine::ecs::PrototypeInstance;
using engine::ecs::World;
using engine::ecs::parse_prototype_asset;
using engine::ecs::resolve_prototype_asset;
using engine::ecs::serialize_prototype_asset;
using engine::ecs::spawn_prototype;

constexpr std::string_view kBaseGuid = "11111111111111111111111111111111";
constexpr std::string_view kVariantGuid = "22222222222222222222222222222222";
constexpr std::string_view kTexGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

class SilentFatalError final : public engine::IFatalError {
public:
    [[noreturn]] void report(std::string_view message) override {
        last_message = std::string(message);
        std::abort();
    }
    std::string last_message;
};

TEST(PrototypeTest, MergeFieldTablesDeepMergesTablesAndReplacesScalars) {
    FieldTable base;
    base["speed"] = FieldValue{10.0};
    base["health"] = FieldValue{100};

    FieldTable sub_base;
    sub_base["x"] = FieldValue{1.0};
    sub_base["y"] = FieldValue{2.0};
    base["nested"] = FieldValue{sub_base};

    FieldTable overrides;
    overrides["speed"] = FieldValue{25.0};

    FieldTable sub_overrides;
    sub_overrides["y"] = FieldValue{50.0};
    sub_overrides["z"] = FieldValue{100.0};
    overrides["nested"] = FieldValue{sub_overrides};

    overrides["armor"] = FieldValue{5.0};

    engine::ecs::merge_field_tables(base, overrides);

    EXPECT_DOUBLE_EQ(*base["speed"].as_number(), 25.0);
    EXPECT_EQ(*base["health"].as_int(), 100);
    EXPECT_DOUBLE_EQ(*base["armor"].as_number(), 5.0);

    const FieldTable* nested = base["nested"].as_table();
    ASSERT_NE(nested, nullptr);
    EXPECT_DOUBLE_EQ(*nested->at("x").as_number(), 1.0);
    EXPECT_DOUBLE_EQ(*nested->at("y").as_number(), 50.0);
    EXPECT_DOUBLE_EQ(*nested->at("z").as_number(), 100.0);
}

TEST(PrototypeTest, ParsesAndSerializesBasicPrototype) {
    constexpr std::string_view kToml = R"(version = 1

[[entities]]
id = "root"

[entities.transform]
position = [1.0, 2.0, 3.0]
scale = [1.0, 1.0, 1.0]

[entities.sprite]
texture = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
)";

    auto parsed = parse_prototype_asset(kToml);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

    EXPECT_EQ(parsed->version, 1);
    EXPECT_FALSE(parsed->base.has_value());
    ASSERT_EQ(parsed->entities.size(), 1u);

    const auto& root = parsed->entities.front();
    EXPECT_EQ(root.id, "root");
    EXPECT_TRUE(root.parent.empty());
    EXPECT_FALSE(root.prototype.has_value());
    EXPECT_TRUE(root.components.contains("transform"));
    EXPECT_TRUE(root.components.contains("sprite"));

    const std::string serialized = serialize_prototype_asset(*parsed);
    auto reloaded = parse_prototype_asset(serialized);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    EXPECT_EQ(*parsed, *reloaded);
}

TEST(PrototypeTest, ParsesPrototypeVariantWithHierarchyAndBase) {
    const std::string toml = std::format(
            R"(version = 1
base = "asset://{}"

[[entities]]
id = "root"

[entities.health]
max = 200

[[entities]]
id = "weapon"
parent = "root"

[entities.sprite]
texture = "{}"
)",
            kBaseGuid, kTexGuid);

    auto parsed = parse_prototype_asset(toml);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

    ASSERT_TRUE(parsed->base.has_value());
    EXPECT_EQ(parsed->base->hex(), kBaseGuid);
    ASSERT_EQ(parsed->entities.size(), 2u);

    EXPECT_EQ(parsed->entities[0].id, "root");
    EXPECT_EQ(parsed->entities[1].id, "weapon");
    EXPECT_EQ(parsed->entities[1].parent, "root");

    const std::string serialized = serialize_prototype_asset(*parsed);
    auto reloaded = parse_prototype_asset(serialized);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    EXPECT_EQ(*parsed, *reloaded);
}

TEST(PrototypeTest, RejectsInvalidPrototypes) {
    {
        auto res = parse_prototype_asset("version = 2\n");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, PrototypeErrorKind::UnsupportedVersion);
    }

    {
        auto res = parse_prototype_asset("base = \"not_a_guid\"\n");
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, PrototypeErrorKind::InvalidField);
    }

    {
        constexpr std::string_view kDup = R"(version = 1
[[entities]]
id = "child"
[[entities]]
id = "child"
)";
        auto res = parse_prototype_asset(kDup);
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, PrototypeErrorKind::DuplicateEntityId);
    }

    {
        constexpr std::string_view kUnknownParent = R"(version = 1
[[entities]]
id = "child"
parent = "missing_parent"
)";
        auto res = parse_prototype_asset(kUnknownParent);
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, PrototypeErrorKind::UnknownParent);
    }

    {
        constexpr std::string_view kCycle = R"(version = 1
[[entities]]
id = "a"
parent = "b"
[[entities]]
id = "b"
parent = "a"
)";
        auto res = parse_prototype_asset(kCycle);
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error().kind, PrototypeErrorKind::HierarchyCycle);
    }
}

TEST(PrototypeTest, ResolvesVariantAndIntegratesIntoWorldAndCppSpawn) {
    const auto dir = std::filesystem::temp_directory_path() / "wind_prototype_integration_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);

    // 1. Base prototype: "orc_base.prototype"
    // root (transform with scale=[1,1,1] and position=[0,0,0], rigid_body_2d)
    // weapon (parent="root", transform with position=[1,0,0])
    {
        std::ofstream base_out(dir / "orc_base.prototype");
        base_out << R"(version = 1

[[entities]]
id = "root"
transform = { position = [0.0, 0.0, 0.0], scale = [1.0, 1.0, 1.0] }
rigid_body_2d = { velocity = [2.0, 0.0, 0.0] }

[[entities]]
id = "weapon"
parent = "root"
transform = { position = [1.0, 0.0, 0.0] }
)";
    }

    // 2. Variant prototype: "orc_armored.prototype"
    // base = kBaseGuid
    // root (overrides position=[10,0,0])
    // shield (adds child entity with position=[-1,0,0])
    {
        std::ofstream var_out(dir / "orc_armored.prototype");
        var_out << "version = 1\nbase = \"asset://" << kBaseGuid << "\"\n\n"
                << "[[entities]]\nid = \"root\"\n"
                << "transform = { position = [10.0, 0.0, 0.0] }\n\n"
                << "[[entities]]\nid = \"shield\"\nparent = \"root\"\n"
                << "transform = { position = [-1.0, 0.0, 0.0] }\n";
    }

    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(dir);

    engine::CookedCatalog catalog;
    catalog.add(engine::CatalogEntry{
            .guid = AssetId{kBaseGuid},
            .relative_path = "orc_base.prototype",
            .importer = engine::ImporterKind::Prototype,
    });
    catalog.add(engine::CatalogEntry{
            .guid = AssetId{kVariantGuid},
            .relative_path = "orc_armored.prototype",
            .importer = engine::ImporterKind::Prototype,
    });
    db.set_catalog(std::move(catalog));

    // Test resolving the variant through AssetsDb
    const auto var_asset = db.try_get<PrototypeAsset>(AssetId{kVariantGuid});
    ASSERT_TRUE(var_asset.has_value());

    auto resolved = resolve_prototype_asset(**var_asset, &db);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_FALSE(resolved->base.has_value());
    ASSERT_EQ(resolved->entities.size(), 3u); // root, weapon, shield

    // Root entity verification in resolved prototype
    const auto& r_root = resolved->entities[0];
    EXPECT_EQ(r_root.id, "root");
    EXPECT_TRUE(r_root.components.contains("transform"));
    EXPECT_TRUE(r_root.components.contains("rigid_body_2d"));
    const FieldTable& root_tr_tab = r_root.components.at("transform");
    const auto* pos_arr = root_tr_tab.at("position").as_array();
    ASSERT_NE(pos_arr, nullptr);
    EXPECT_DOUBLE_EQ(*pos_arr->at(0).as_number(), 10.0); // overridden
    const auto* scale_arr = root_tr_tab.at("scale").as_array();
    ASSERT_NE(scale_arr, nullptr);
    EXPECT_DOUBLE_EQ(*scale_arr->at(0).as_number(), 1.0); // preserved from base

    // 3. Test instantiating a .world file containing an instance of this prototype
    const std::string world_toml = std::string("version = 1\n\n[[entities]]\nid = \"boss\"\nprototype = \"asset://") +
            std::string(kVariantGuid) +
            "\"\ntransform = { position = [100.0, 200.0, 0.0] }\n\n"
            "[[entities]]\nid = \"boss/shield\"\ntransform = { scale = [3.0, 3.0, 3.0] }\n";

    World world;
    auto loaded = engine::ecs::load_world_from_toml(world, world_toml, &db);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    // Check spawned entities
    ASSERT_TRUE(loaded->by_id.contains("boss"));
    ASSERT_TRUE(loaded->by_id.contains("boss/weapon"));
    ASSERT_TRUE(loaded->by_id.contains("boss/shield"));

    const Entity boss_e = loaded->by_id.at("boss");
    const Entity weapon_e = loaded->by_id.at("boss/weapon");
    const Entity shield_e = loaded->by_id.at("boss/shield");

    // Check PrototypeInstance component
    const auto* boss_proto_inst = world.try_get<PrototypeInstance>(boss_e);
    ASSERT_NE(boss_proto_inst, nullptr);
    EXPECT_EQ(boss_proto_inst->prototype, AssetId{kVariantGuid});
    EXPECT_EQ(boss_proto_inst->entity_sub_id, "root");

    const auto* shield_proto_inst = world.try_get<PrototypeInstance>(shield_e);
    ASSERT_NE(shield_proto_inst, nullptr);
    EXPECT_EQ(shield_proto_inst->prototype, AssetId{kVariantGuid});
    EXPECT_EQ(shield_proto_inst->entity_sub_id, "shield");

    // Check Transform positions & overrides
    const auto* boss_tr = world.try_get<engine::Transform>(boss_e);
    ASSERT_NE(boss_tr, nullptr);
    EXPECT_FLOAT_EQ(boss_tr->position.x, 100.0f);
    EXPECT_FLOAT_EQ(boss_tr->position.y, 200.0f);
    EXPECT_FLOAT_EQ(boss_tr->scale.x, 1.0f); // inherited from base

    const auto* shield_tr = world.try_get<engine::Transform>(shield_e);
    ASSERT_NE(shield_tr, nullptr);
    EXPECT_FLOAT_EQ(shield_tr->position.x, -1.0f); // from variant
    EXPECT_FLOAT_EQ(shield_tr->scale.x, 3.0f); // overridden in world scene!

    // Check hierarchy: weapon and shield are children of boss
    const auto* weapon_parent = world.try_get<engine::Parent>(weapon_e);
    ASSERT_NE(weapon_parent, nullptr);
    EXPECT_EQ(weapon_parent->entity, boss_e);

    const auto* shield_parent = world.try_get<engine::Parent>(shield_e);
    ASSERT_NE(shield_parent, nullptr);
    EXPECT_EQ(shield_parent->entity, boss_e);

    // WorldTransform computation
    const glm::mat4 weapon_world_m = engine::ecs::world_matrix(world, weapon_e);
    EXPECT_FLOAT_EQ(weapon_world_m[3][0], 101.0f); // 100 + 1
    EXPECT_FLOAT_EQ(weapon_world_m[3][1], 200.0f);

    // 4. Test C++ spawn_prototype helper
    World world2;
    auto spawned_root = spawn_prototype(
            world2, AssetId{kBaseGuid}, db, {.position = glm::vec3{5.0f, 6.0f, 7.0f}, .name = "player"});
    ASSERT_TRUE(spawned_root.has_value()) << spawned_root.error().message;

    const auto* spawned_tr = world2.try_get<engine::Transform>(*spawned_root);
    ASSERT_NE(spawned_tr, nullptr);
    EXPECT_FLOAT_EQ(spawned_tr->position.x, 5.0f);
    EXPECT_FLOAT_EQ(spawned_tr->position.y, 6.0f);
    EXPECT_FLOAT_EQ(spawned_tr->position.z, 7.0f);

    const auto* player_proto_inst = world2.try_get<PrototypeInstance>(*spawned_root);
    ASSERT_NE(player_proto_inst, nullptr);
    EXPECT_EQ(player_proto_inst->prototype, AssetId{kBaseGuid});
    EXPECT_EQ(player_proto_inst->entity_sub_id, "root");

    std::filesystem::remove_all(dir, ec);
}

TEST(PrototypeTest, DetectsCycleInVariantInheritance) {
    const auto dir = std::filesystem::temp_directory_path() / "wind_prototype_cycle_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);

    constexpr std::string_view kGuidA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr std::string_view kGuidB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    // A points to B
    {
        std::ofstream out(dir / "a.prototype");
        out << std::format("version = 1\nbase = \"asset://{}\"\n[[entities]]\nid = \"root\"\n", kGuidB);
    }
    // B points to A
    {
        std::ofstream out(dir / "b.prototype");
        out << std::format("version = 1\nbase = \"asset://{}\"\n[[entities]]\nid = \"root\"\n", kGuidA);
    }

    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(dir);

    engine::CookedCatalog catalog;
    catalog.add(engine::CatalogEntry{
            .guid = AssetId{kGuidA},
            .relative_path = "a.prototype",
            .importer = engine::ImporterKind::Prototype,
    });
    catalog.add(engine::CatalogEntry{
            .guid = AssetId{kGuidB},
            .relative_path = "b.prototype",
            .importer = engine::ImporterKind::Prototype,
    });
    db.set_catalog(std::move(catalog));

    auto asset_a = db.try_get<PrototypeAsset>(AssetId{kGuidA});
    ASSERT_TRUE(asset_a.has_value());

    auto resolved = resolve_prototype_asset(**asset_a, &db);
    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().kind, PrototypeErrorKind::PrototypeCycle);

    std::filesystem::remove_all(dir, ec);
}

} // namespace
