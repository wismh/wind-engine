#include <gtest/gtest.h>

#include "fixtures/fatal_fakes.h"

#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>
#include <engine/ecs/world_serializer.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/text_asset.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace game {

struct ItemDrop {
    engine::AssetId item{};
    int count = 1;
};

inline engine::ecs::FieldValue encode_wind(const engine::ecs::ComponentWriter& writer, const ItemDrop& value) {
    engine::ecs::FieldTable table;
    engine::ecs::ComponentWriter nested = writer.nested(table);
    nested.field("item", value.item);
    nested.field("count", value.count);
    return engine::ecs::FieldValue{std::move(table)};
}

inline bool decode_wind(const engine::ecs::ComponentReader& reader, const engine::ecs::FieldValue& node,
                        ItemDrop& value) {
    const engine::ecs::FieldTable* table = node.as_table();
    if (table == nullptr) {
        return false;
    }
    engine::ecs::ComponentReader nested = reader.nested(*table);
    (void)nested.field("item", value.item);
    (void)nested.field("count", value.count);
    return nested.ok();
}

struct Grant {
    ItemDrop drop{};
    std::vector<ItemDrop> extras{};
    engine::AssetId loot{};
};

void register_grant(engine::ecs::WorldSerializer& serializer) {
    serializer.register_component<Grant>(
        "grant",
        [](engine::ecs::ComponentWriter& out, const Grant& grant) {
            out.field("drop", grant.drop);
            out.field("extras", grant.extras);
            out.field("loot", grant.loot);
        },
        [](engine::ecs::ComponentReader& in, Grant& grant) {
            (void)in.field("drop", grant.drop);
            (void)in.field("extras", grant.extras);
            (void)in.field("loot", grant.loot);
        });
}

struct Sword {
    int count = 1;
    std::string name;
};

} // namespace game

namespace {

constexpr std::string_view kSprite = "0123456789abcdef0123456789abcdef";
constexpr std::string_view kSwordGuid = "abcdef0123456789abcdef0123456789";

class TempDir {
public:
    TempDir() {
        const auto* const info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = std::filesystem::temp_directory_path() / "wind_serializable_object" / info->name();
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
}

} // namespace

TEST(SerializableObject, NestedValueRoundTripsInsideTheComponentFile) {
    constexpr std::string_view kToml = R"(
version = 1

[[entities]]
id = "loot"

[entities.grant]
loot = "fedcba9876543210fedcba9876543210"

[entities.grant.drop]
item = "0123456789abcdef0123456789abcdef"
count = 3

[[entities.grant.extras]]
count = 2
)";

    engine::ecs::World world;
    game::register_grant(world.ctx<engine::ecs::WorldSerializer>());
    const auto loaded = engine::ecs::load_world_from_toml(world, kToml);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    const auto* grant = world.try_get<game::Grant>(loaded->by_id.at("loot"));
    ASSERT_NE(grant, nullptr);
    EXPECT_EQ(grant->loot, engine::AssetId{"fedcba9876543210fedcba9876543210"});
    EXPECT_EQ(grant->drop.item, engine::AssetId{kSprite});
    EXPECT_EQ(grant->drop.count, 3);
    ASSERT_EQ(grant->extras.size(), 1u);
    EXPECT_EQ(grant->extras[0].item, engine::AssetId{});
    EXPECT_EQ(grant->extras[0].count, 2);

    const std::string saved = engine::ecs::save_world_to_toml(world);
    EXPECT_NE(saved.find("fedcba9876543210fedcba9876543210"), std::string::npos);

    engine::ecs::World again;
    game::register_grant(again.ctx<engine::ecs::WorldSerializer>());
    const auto reloaded = engine::ecs::load_world_from_toml(again, saved);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    const auto* round = again.try_get<game::Grant>(reloaded->by_id.at("loot"));
    ASSERT_NE(round, nullptr);
    EXPECT_EQ(round->loot, grant->loot);
    EXPECT_EQ(round->drop.item, grant->drop.item);
    EXPECT_EQ(round->drop.count, grant->drop.count);
    ASSERT_EQ(round->extras.size(), 1u);
    EXPECT_EQ(round->extras[0].count, 2);
    EXPECT_EQ(round->extras[0].item, engine::AssetId{});
}

TEST(SerializableObject, OmittedNestedFieldKeepsItsDefault) {
    constexpr std::string_view kToml = R"(
version = 1

[[entities]]
id = "loot"
grant = { drop = { item = "0123456789abcdef0123456789abcdef" } }
)";

    engine::ecs::World world;
    game::register_grant(world.ctx<engine::ecs::WorldSerializer>());
    const auto loaded = engine::ecs::load_world_from_toml(world, kToml);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    const auto* grant = world.try_get<game::Grant>(loaded->by_id.at("loot"));
    ASSERT_NE(grant, nullptr);
    EXPECT_EQ(grant->drop.count, 1);
    EXPECT_TRUE(grant->extras.empty());
    EXPECT_EQ(grant->loot, engine::AssetId{});
}

TEST(SerializableObject, AssetsDbLoadsClearsAndRejectsABadObjectFile) {
    TempDir dir;
    write_file(dir.path() / "items" / "sword.object", "type = \"sword\"\nname = \"blade\"\ncount = 4\n");
    write_file(dir.path() / "items" / "other.object", "type = \"other\"\nname = \"nope\"\n");
    write_file(dir.path() / "items" / "broken.object", "not toml ===\n");
    write_file(dir.path() / "items" / "bare.object", "name = \"bare\"\n");

    fakes::QuietFatal fatal;
    engine::AssetsDb db(fatal);
    db.set_root(dir.path());

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kSwordGuid}, "items/sword.object", engine::ImporterKind::Object});
    catalog.add(
        {engine::AssetId{"11111111111111111111111111111111"}, "items/other.object", engine::ImporterKind::Object});
    catalog.add(
        {engine::AssetId{"22222222222222222222222222222222"}, "items/broken.object", engine::ImporterKind::Object});
    catalog.add(
        {engine::AssetId{"33333333333333333333333333333333"}, "items/bare.object", engine::ImporterKind::Object});
    db.set_catalog(std::move(catalog));

    const engine::AssetId sword{kSwordGuid};
    const auto missing_loader = db.try_get<game::Sword>(sword);
    ASSERT_FALSE(missing_loader.has_value());
    EXPECT_EQ(missing_loader.error(), engine::AssetError::TypeMismatch);

    db.register_object<game::Sword>("sword", [](engine::ecs::ComponentReader& in, game::Sword& sword_value) {
        (void)in.field("count", sword_value.count);
        (void)in.field("name", sword_value.name);
    });

    const auto loaded = db.get<game::Sword>(sword);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->name, "blade");
    EXPECT_EQ(loaded->count, 4);
    EXPECT_EQ(db.get<game::Sword>(sword), loaded);

    const auto as_text = db.try_get<engine::TextAsset>(sword);
    ASSERT_FALSE(as_text.has_value());
    EXPECT_EQ(as_text.error(), engine::AssetError::TypeMismatch);

    const auto wrong_type = db.try_get<game::Sword>(engine::AssetId{"11111111111111111111111111111111"});
    ASSERT_FALSE(wrong_type.has_value());
    EXPECT_EQ(wrong_type.error(), engine::AssetError::TypeMismatch);

    const auto broken = db.try_get<game::Sword>(engine::AssetId{"22222222222222222222222222222222"});
    ASSERT_FALSE(broken.has_value());
    EXPECT_EQ(broken.error(), engine::AssetError::Corrupt);

    const auto bare = db.try_get<game::Sword>(engine::AssetId{"33333333333333333333333333333333"});
    ASSERT_FALSE(bare.has_value());
    EXPECT_EQ(bare.error(), engine::AssetError::Corrupt);

    db.clear_object_loaders();
    const auto after_clear = db.try_get<game::Sword>(sword);
    ASSERT_FALSE(after_clear.has_value());
    EXPECT_EQ(after_clear.error(), engine::AssetError::TypeMismatch);
    EXPECT_EQ(loaded->name, "blade");
}
