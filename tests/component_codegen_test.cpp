#include <engine/resources/asset_guid.h>
#include <engine/resources/component_codegen.h>
#include <engine/resources/meta.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

TEST(ComponentCodegen, ScansWindComponentStructsAcrossNamespacesAndIgnoresMethodsAndStatics) {
    constexpr std::string_view kHeader = R"cpp(
#pragma once
#include <engine/ecs/world_io.h>

// WIND_COMPONENT in a comment is ignored
/* struct WIND_COMPONENT IgnoredInBlockComment { int x; }; */

namespace game {

struct HelperNotAComponent {
    int ignored = 0;
};

struct WIND_COMPONENT EnemySpawner {
    static constexpr int kMax = 10;
    using Alias = int;

    float interval = 2.5f;
    int max_count{5};
    engine::AssetId enemy_sprite;
    std::vector<glm::vec2> waypoints{};

    void reset() {
        max_count = 0;
    }
    [[nodiscard]] bool ready() const noexcept;
};

namespace ai::patrol {

struct WIND_COMPONENT("custom_patrol") PatrolNode2D {
    engine::ecs::Entity next_node{};
    float wait_seconds = 1.0f;
};

struct WIND_COMPONENT EmptyTag {};

struct WIND_COMPONENT ForwardDeclaredOnly;

} // namespace ai::patrol
} // namespace game
)cpp";

    const auto scanned = engine::scan_components_in_text(kHeader, "gameplay/spawner.h");
    ASSERT_TRUE(scanned.has_value()) << scanned.error().message;
    ASSERT_EQ(scanned->components.size(), 3u);
    EXPECT_TRUE(scanned->serializables.empty());

    EXPECT_EQ(scanned->components[0].qualified_name(), "game::EnemySpawner");
    EXPECT_EQ(scanned->components[0].toml_key, "enemy_spawner");
    EXPECT_EQ(scanned->components[0].fields,
              (std::vector<std::string>{"interval", "max_count", "enemy_sprite", "waypoints"}));

    EXPECT_EQ(scanned->components[1].qualified_name(), "game::ai::patrol::PatrolNode2D");
    EXPECT_EQ(scanned->components[1].toml_key, "custom_patrol");
    EXPECT_EQ(scanned->components[1].fields, (std::vector<std::string>{"next_node", "wait_seconds"}));

    EXPECT_EQ(scanned->components[2].qualified_name(), "game::ai::patrol::EmptyTag");
    EXPECT_EQ(scanned->components[2].toml_key, "empty_tag");
    EXPECT_TRUE(scanned->components[2].fields.empty());

    const std::string emitted = engine::emit_component_io_header(scanned->components, scanned->serializables);
    EXPECT_NE(emitted.find("#include \"gameplay/spawner.h\""), std::string::npos);
    EXPECT_NE(emitted.find("serializer.register_component<game::EnemySpawner>"), std::string::npos);
    EXPECT_NE(emitted.find("out.field(\"interval\", c.interval);"), std::string::npos);
    EXPECT_NE(emitted.find("in.field(\"waypoints\", c.waypoints);"), std::string::npos);
    EXPECT_NE(emitted.find("\"custom_patrol\""), std::string::npos);
    EXPECT_NE(emitted.find("inline void register_game_objects(engine::AssetsDb&) {}"), std::string::npos);
}

TEST(ComponentCodegen, ScansSerializableStructsAndRejectsSharedMarkersAndTypeField) {
    constexpr std::string_view kHeader = R"cpp(
namespace game {
struct WIND_SERIALIZABLE("item_drop") ItemDrop {
    engine::AssetId item;
    int count = 1;
};
struct WIND_COMPONENT Chest {
    engine::AssetId loot;
};
struct WIND_SERIALIZABLE ForwardOnly;
}
)cpp";

    const auto scanned = engine::scan_components_in_text(kHeader, "items.h");
    ASSERT_TRUE(scanned.has_value()) << scanned.error().message;
    ASSERT_EQ(scanned->serializables.size(), 1u);
    ASSERT_EQ(scanned->components.size(), 1u);
    EXPECT_EQ(scanned->serializables[0].qualified_name(), "game::ItemDrop");
    EXPECT_EQ(scanned->serializables[0].toml_key, "item_drop");
    EXPECT_EQ(scanned->serializables[0].fields, (std::vector<std::string>{"item", "count"}));
    EXPECT_EQ(scanned->components[0].toml_key, "chest");

    const std::string emitted = engine::emit_component_io_header(scanned->components, scanned->serializables);
    EXPECT_NE(emitted.find("namespace game {"), std::string::npos);
    EXPECT_NE(emitted.find("encode_wind(const engine::ecs::ComponentWriter& w, const game::ItemDrop& v)"),
              std::string::npos);
    EXPECT_NE(emitted.find("decode_wind(const engine::ecs::ComponentReader& r, const engine::ecs::FieldValue& node, "
                           "game::ItemDrop& v)"),
              std::string::npos);
    EXPECT_NE(emitted.find("db.register_object<game::ItemDrop>"), std::string::npos);
    EXPECT_NE(emitted.find("game::read_fields(in, v);"), std::string::npos);
    EXPECT_EQ(emitted.find("out.field(\"type\""), std::string::npos);

    constexpr std::string_view kBoth = "struct WIND_COMPONENT WIND_SERIALIZABLE Both { int n; };";
    const auto both = engine::scan_components_in_text(kBoth, "both.h");
    ASSERT_FALSE(both.has_value());
    EXPECT_EQ(both.error().kind, engine::CodegenErrorKind::ComponentCodegen);

    constexpr std::string_view kType = "struct WIND_SERIALIZABLE Tagged { std::string type; int n; };";
    const auto typed = engine::scan_components_in_text(kType, "tagged.h");
    ASSERT_FALSE(typed.has_value());
    EXPECT_EQ(typed.error().kind, engine::CodegenErrorKind::ComponentCodegen);
    EXPECT_NE(typed.error().message.find("\"type\""), std::string::npos);
}

TEST(ComponentCodegen, WritesComponentIoHeaderAndDetectsKeyCollision) {
    const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "wind_component_codegen_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "src");

    {
        std::ofstream out(dir / "src" / "a.h");
        out << "namespace a { struct WIND_COMPONENT Health { int hp = 100; }; }\n";
    }
    const std::filesystem::path out_header = dir / "generated" / "component_io.h";
    const std::vector<std::filesystem::path> dirs{dir / "src"};
    const auto write_ok = engine::component_codegen_write(dirs, out_header);
    ASSERT_TRUE(write_ok.has_value()) << write_ok.error().message;
    EXPECT_TRUE(std::filesystem::exists(out_header));

    // Add a second component with the same snake_case key in another namespace -> collision.
    {
        std::ofstream out(dir / "src" / "b.h");
        out << "namespace b { struct WIND_COMPONENT Health { int hp = 50; }; }\n";
    }
    const auto collision = engine::component_codegen_write(dirs, out_header);
    ASSERT_FALSE(collision.has_value());
    EXPECT_EQ(collision.error().kind, engine::CodegenErrorKind::Collision);

    {
        std::ofstream out(dir / "src" / "b.h", std::ios::trunc);
        out << "namespace b { struct WIND_SERIALIZABLE Health { int hp = 50; }; }\n";
    }
    {
        std::ofstream out(dir / "src" / "c.h");
        out << "namespace c { struct WIND_SERIALIZABLE Health { int hp = 7; }; }\n";
    }
    const auto serializable_collision = engine::component_codegen_write(dirs, out_header);
    ASSERT_FALSE(serializable_collision.has_value());
    EXPECT_EQ(serializable_collision.error().kind, engine::CodegenErrorKind::Collision);

    std::filesystem::remove(dir / "src" / "c.h");
    const auto shared_key = engine::component_codegen_write(dirs, out_header);
    ASSERT_TRUE(shared_key.has_value()) << shared_key.error().message;

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(ComponentCodegen, AssetGuidAndCodegenScanHandleWorldAssets) {
    const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "wind_world_codegen_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "levels");

    {
        std::ofstream out(dir / "levels" / "level_01.world");
        out << "version = 1\n[[entities]]\nid = \"cam\"\n";
    }

    const int metas_written = engine::write_missing_metas(dir);
    EXPECT_EQ(metas_written, 1);
    EXPECT_TRUE(std::filesystem::exists(dir / "levels" / "level_01.world.meta"));

    const auto scanned = engine::codegen_scan(dir);
    ASSERT_TRUE(scanned.has_value()) << scanned.error().message;
    EXPECT_NE(
            scanned->asset_ids_header.find("inline constexpr engine::AssetId level_01"),
            std::string::npos);

    // Corrupt .world file fails codegen_scan with CodegenErrorKind::World.
    {
        std::ofstream out(dir / "levels" / "level_01.world", std::ios::trunc);
        out << "version = 1\nactive_camera = \"missing\"\n";
    }
    const auto bad_scan = engine::codegen_scan(dir);
    ASSERT_FALSE(bad_scan.has_value());
    EXPECT_EQ(bad_scan.error().kind, engine::CodegenErrorKind::World);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(ComponentCodegen, ObjectFilesNeedATypeKeyAndUseTheObjectImporter) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wind_object_codegen_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "items");
    {
        std::ofstream out(dir / "items" / "sword.object");
        out << "type = \"item_drop\"\ncount = 1\n";
    }

    EXPECT_EQ(engine::write_missing_metas(dir), 1);
    std::string meta_text;
    {
        std::ifstream in(dir / "items" / "sword.object.meta");
        meta_text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const auto meta = engine::parse_asset_meta(meta_text);
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->importer, engine::ImporterKind::Object);

    const auto scanned = engine::codegen_scan(dir);
    ASSERT_TRUE(scanned.has_value()) << scanned.error().message;
    ASSERT_EQ(scanned->catalog.entries().size(), 1u);
    EXPECT_EQ(scanned->catalog.entries()[0].importer, engine::ImporterKind::Object);

    {
        std::ofstream out(dir / "items" / "sword.object", std::ios::trunc);
        out << "count = 1\n";
    }
    const auto missing_type = engine::codegen_scan(dir);
    ASSERT_FALSE(missing_type.has_value());
    EXPECT_EQ(missing_type.error().kind, engine::CodegenErrorKind::Object);

    {
        std::ofstream out(dir / "items" / "sword.object", std::ios::trunc);
        out << "not toml ===\n";
    }
    const auto broken = engine::codegen_scan(dir);
    ASSERT_FALSE(broken.has_value());
    EXPECT_EQ(broken.error().kind, engine::CodegenErrorKind::Object);

    std::error_code remove_ec;
    std::filesystem::remove_all(dir, remove_ec);
}
