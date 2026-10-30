#include <gtest/gtest.h>

#include "fixtures/fatal_fakes.h"

#include <engine/ecs/world_io.h>
#include <engine/resources/asset_filter.h>
#include <engine/resources/asset_ref.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/text_asset.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace test_types {

struct WeaponItem {
    std::string name;
    int damage = 10;
};

} // namespace test_types

template<>
struct engine::CustomAssetFilter<test_types::WeaponItem> {
    static constexpr bool kHas = true;
    static AssetFilter filter() {
        return AssetFilter{.importer = ImporterKind::Object, .object_type = "weapon_item"};
    }
};

namespace {

constexpr std::string_view kTexGuid = "a0e1b2c3d4f5678901234567890abc01";
constexpr std::string_view kItemGuid = "11112222333344445555666677778888";

class TempDir {
public:
    TempDir() {
        const auto* const info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = std::filesystem::temp_directory_path() / "wind_asset_ref_test" / info->name();
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

TEST(AssetRefTest, DefaultConstructorIsEmptyAndInvalid) {
    const engine::AssetRef<engine::render::ITexture> ref;
    EXPECT_FALSE(ref.valid());
    EXPECT_EQ(ref.id(), engine::AssetId{});
    EXPECT_EQ(ref.guid(), engine::AssetId{});
    EXPECT_EQ(static_cast<engine::AssetId>(ref), engine::AssetId{});
}

TEST(AssetRefTest, ConstructWithGuidOrHex) {
    const engine::AssetId id{kTexGuid};
    const engine::AssetRef<engine::render::ITexture> from_id{id};
    const engine::AssetRef<engine::render::ITexture> from_hex{kTexGuid};

    EXPECT_TRUE(from_id.valid());
    EXPECT_TRUE(from_hex.valid());
    EXPECT_EQ(from_id.id(), id);
    EXPECT_EQ(from_hex.id(), id);
    EXPECT_EQ(from_id, from_hex);
}

TEST(AssetRefTest, FilterForEngineTypes) {
    const engine::AssetFilter tex_filter = engine::AssetRef<engine::render::ITexture>::filter();
    EXPECT_EQ(tex_filter.importer, engine::ImporterKind::Texture);
    EXPECT_TRUE(tex_filter.object_type.empty());

    const engine::AssetFilter mesh_filter = engine::asset_filter_for<engine::render::IMesh>();
    EXPECT_EQ(mesh_filter.importer, engine::ImporterKind::Mesh);

    const engine::AssetFilter mat_filter = engine::asset_filter_for<engine::render::IMaterial>();
    EXPECT_EQ(mat_filter.importer, engine::ImporterKind::Material);

    const engine::AssetFilter sound_filter = engine::asset_filter_for<engine::Sound>();
    EXPECT_EQ(sound_filter.importer, engine::ImporterKind::Audio);

    const engine::AssetFilter font_filter = engine::asset_filter_for<engine::Font>();
    EXPECT_EQ(font_filter.importer, engine::ImporterKind::Font);
}

TEST(AssetRefTest, FilterForCustomObjectStruct) {
    const engine::AssetFilter item_filter = engine::AssetRef<test_types::WeaponItem>::filter();
    EXPECT_EQ(item_filter.importer, engine::ImporterKind::Object);
    EXPECT_EQ(item_filter.object_type, "weapon_item");
}

TEST(AssetRefTest, SerializationRoundTrip) {
    engine::ecs::FieldTable table;
    engine::ecs::ComponentWriter writer{table};

    const engine::AssetRef<test_types::WeaponItem> original{kItemGuid};
    writer.field("weapon", original);

    const auto found = table.find("weapon");
    ASSERT_NE(found, table.end());
    const std::string* str = found->second.as_string();
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(*str, kItemGuid);

    engine::ecs::ComponentReader reader{table};
    engine::AssetRef<test_types::WeaponItem> decoded;
    EXPECT_TRUE(reader.field("weapon", decoded));
    EXPECT_EQ(decoded, original);
}

TEST(AssetRefTest, EmptySerializationRoundTrip) {
    engine::ecs::FieldTable table;
    engine::ecs::ComponentWriter writer{table};

    const engine::AssetRef<engine::render::ITexture> empty_ref;
    writer.field("texture", empty_ref);

    const auto found = table.find("texture");
    ASSERT_NE(found, table.end());
    const std::string* str = found->second.as_string();
    ASSERT_NE(str, nullptr);
    EXPECT_TRUE(str->empty());

    engine::ecs::ComponentReader reader{table};
    engine::AssetRef<engine::render::ITexture> decoded{kTexGuid};
    EXPECT_TRUE(reader.field("texture", decoded));
    EXPECT_FALSE(decoded.valid());
    EXPECT_EQ(decoded.id(), engine::AssetId{});
}

TEST(AssetRefTest, ResolvesViaAssetsDb) {
    const TempDir temp;
    const std::filesystem::path obj_path = temp.path() / "weapons" / "excalibur.object";
    write_file(obj_path, "type = \"weapon_item\"\nname = \"Excalibur\"\ndamage = 99\n");

    fakes::QuietFatal fatal;
    engine::AssetsDb db{fatal};
    db.set_root(temp.path());

    bool loaded = false;
    db.register_object<test_types::WeaponItem>(
            "weapon_item",
            [&loaded](engine::ecs::ComponentReader& in, test_types::WeaponItem& item) {
                loaded = true;
                (void)in.field("name", item.name);
                (void)in.field("damage", item.damage);
            });

    engine::CookedCatalog catalog;
    catalog.add(engine::CatalogEntry{
            .guid = engine::AssetId{kItemGuid},
            .relative_path = "weapons/excalibur.object",
            .importer = engine::ImporterKind::Object,
            .files_root = temp.path(),
    });
    db.set_catalog(std::move(catalog));

    const engine::AssetRef<test_types::WeaponItem> ref{kItemGuid};
    auto result = ref.try_get(db);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(loaded);
    EXPECT_EQ((*result)->name, "Excalibur");
    EXPECT_EQ((*result)->damage, 99);

    auto direct = ref.get(db);
    ASSERT_NE(direct, nullptr);
    EXPECT_EQ(direct->name, "Excalibur");
}

} // namespace
