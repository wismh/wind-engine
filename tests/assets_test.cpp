#include <gtest/gtest.h>

#include <engine/audio/sound.h>
#include <engine/render/graphics.h>
#include <engine/resources/asset_guid.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/meta.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view kTextureGuid = "a1b2c3d4e5f6789012345678901234ab";
constexpr std::string_view kAudioGuid = "b1c2d3e4f567890123456789012345ab";
constexpr std::string_view kUiGuid = "d1e2f3a4567890123456789012345abc";
constexpr std::string_view kCssGuid = "e1f2a3b4567890123456789012345abc";
constexpr std::string_view kTextureToml = R"(
guid = "a1b2c3d4e5f6789012345678901234ab"
importer = "texture"
color_space = "srgb"
filter = "linear"
wrap = "clamp"
layout = "single"
)";
constexpr std::string_view kAudioToml = R"(
guid = "b1c2d3e4f567890123456789012345ab"
importer = "audio"
bank = "sfx"
volume = 0.75
pitch_range = [0.9, 1.1]
loop = true
)";

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
                ("wind_assets_" + std::to_string(stamp) + "_" + std::to_string(++seq));
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

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}

TEST(Assets, ParseTextureTomlMeta) {
    const auto parsed = engine::parse_asset_meta(kTextureToml);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->guid.hex(), kTextureGuid);
    EXPECT_EQ(parsed->importer, engine::ImporterKind::Texture);
    EXPECT_EQ(parsed->texture.color_space, engine::ColorSpace::Srgb);
    EXPECT_EQ(parsed->texture.filter, engine::FilterMode::Linear);
    EXPECT_EQ(parsed->texture.wrap, engine::WrapMode::Clamp);
    EXPECT_EQ(parsed->texture.layout, engine::TextureLayout::Single);

    EXPECT_EQ(engine::parse_asset_meta("importer = \"texture\"\n").error(), engine::MetaError::MissingGuid);
    EXPECT_EQ(engine::parse_asset_meta("guid = \"a1b2c3d4e5f6789012345678901234ab\"\n").error(),
            engine::MetaError::MissingImporter);
}

TEST(Assets, ParseAudioTomlMeta) {
    const auto parsed = engine::parse_asset_meta(kAudioToml);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->guid.hex(), kAudioGuid);
    EXPECT_EQ(parsed->importer, engine::ImporterKind::Audio);
    EXPECT_EQ(parsed->audio.bank, engine::AudioBank::Sfx);
    EXPECT_FLOAT_EQ(parsed->audio.volume, 0.75f);
    EXPECT_FLOAT_EQ(parsed->audio.pitch_min, 0.9f);
    EXPECT_FLOAT_EQ(parsed->audio.pitch_max, 1.1f);
    EXPECT_TRUE(parsed->audio.loop);
}

TEST(Assets, BadGuidRejected) {
    const auto too_short = engine::parse_asset_meta("guid = \"abc\"\nimporter = \"texture\"\n");
    EXPECT_FALSE(too_short.has_value());
    EXPECT_EQ(too_short.error(), engine::MetaError::InvalidGuid);

    const auto uppercase = engine::parse_asset_meta(
            "guid = \"A1B2C3D4E5F6789012345678901234AB\"\nimporter = \"texture\"\n");
    EXPECT_FALSE(uppercase.has_value());
    EXPECT_EQ(uppercase.error(), engine::MetaError::InvalidGuid);

    const auto non_hex = engine::parse_asset_meta(
            "guid = \"g1b2c3d4e5f6789012345678901234ab\"\nimporter = \"texture\"\n");
    EXPECT_FALSE(non_hex.has_value());
    EXPECT_EQ(non_hex.error(), engine::MetaError::InvalidGuid);
}

TEST(Assets, CookedCatalog) {
    const engine::AssetId guid{kTextureGuid};
    engine::CookedCatalog catalog;
    catalog.add({guid, "textures/player.png", engine::ImporterKind::Texture});

    const engine::CatalogEntry* found = catalog.find(guid);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->relative_path, "textures/player.png");
    EXPECT_EQ(found->importer, engine::ImporterKind::Texture);

    const auto loaded = engine::parse_cooked_catalog(catalog.serialize());
    ASSERT_TRUE(loaded.has_value());
    const engine::CatalogEntry* again = loaded->find(guid);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->relative_path, "textures/player.png");
    EXPECT_EQ(again->importer, engine::ImporterKind::Texture);
}

TEST(Assets, TryGetNotFound) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    const auto result = db.TryGet<engine::render::ITexture>(engine::AssetId{kTextureGuid});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::AssetError::NotFound);
}

TEST(Assets, TryGetTypeMismatch) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kAudioGuid}, "sfx/step.wav", engine::ImporterKind::Audio});
    db.set_catalog(std::move(catalog));

    const auto result = db.TryGet<engine::render::ITexture>(engine::AssetId{kAudioGuid});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::AssetError::TypeMismatch);
}

TEST(Assets, GetCallsFatalHook) {
    RecordingFatalError fatal;
    engine::AssetsDb db(fatal);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kTextureGuid}, "textures/player.png", engine::ImporterKind::Texture});
    catalog.add({engine::AssetId{kAudioGuid}, "sfx/step.wav", engine::ImporterKind::Audio});
    db.set_catalog(std::move(catalog));

    const auto not_ready = db.TryGet<engine::render::ITexture>(engine::AssetId{kTextureGuid});
    ASSERT_FALSE(not_ready.has_value());
    EXPECT_EQ(not_ready.error(), engine::AssetError::NotReady);

    bool missing_returned = false;
    try {
        (void) db.Get<engine::render::ITexture>(engine::AssetId{"c1d2e3f4567890123456789012345abc"});
        missing_returned = true;
    } catch (const std::runtime_error&) {
    }
    EXPECT_FALSE(missing_returned);
    EXPECT_EQ(fatal.call_count, 1);
    EXPECT_NE(fatal.last_message.find("c1d2e3f4567890123456789012345abc"), std::string::npos);
    EXPECT_NE(fatal.last_message.find("NotFound"), std::string::npos);

    bool mismatch_returned = false;
    try {
        (void) db.Get<engine::render::ITexture>(engine::AssetId{kAudioGuid});
        mismatch_returned = true;
    } catch (const std::runtime_error&) {
    }
    EXPECT_FALSE(mismatch_returned);
    EXPECT_EQ(fatal.call_count, 2);
    EXPECT_NE(fatal.last_message.find(kAudioGuid), std::string::npos);
    EXPECT_NE(fatal.last_message.find("TypeMismatch"), std::string::npos);
}

TEST(Assets, IdentifierFromPath) {
    const engine::AssetCppId id = engine::identifier_from_path("textures/player.png");
    EXPECT_EQ(id.qualified(), "assets::textures::player");
    ASSERT_EQ(id.namespaces.size(), 2u);
    EXPECT_EQ(id.namespaces[0], "assets");
    EXPECT_EQ(id.namespaces[1], "textures");
    EXPECT_EQ(id.name, "player");
}

TEST(Assets, CodegenFailsIfMetaMissing) {
    TempTree tree;
    write_file(tree.path / "textures" / "player.png", "png");

    const auto result = engine::codegen_scan(tree.path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, engine::CodegenErrorKind::MissingMeta);
}

TEST(Assets, CollisionFails) {
    TempTree tree;
    write_file(tree.path / "textures" / "player.png", "png");
    write_file(tree.path / "textures" / "player.png.meta", kTextureToml);
    write_file(tree.path / "sfx" / "step.wav", "wav");
    write_file(tree.path / "sfx" / "step.wav.meta",
            "guid = \"a1b2c3d4e5f6789012345678901234ab\"\nimporter = \"audio\"\n");

    const auto result = engine::codegen_scan(tree.path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, engine::CodegenErrorKind::Collision);
}

TEST(Assets, GuidWritesMissingMetaOnly) {
    TempTree tree;
    write_file(tree.path / "hud.xml", "<Canvas><Label Text=\"Hi\"/></Canvas>");

    EXPECT_EQ(engine::write_missing_metas(tree.path), 1);
    const std::filesystem::path meta_path = tree.path / "hud.xml.meta";
    ASSERT_TRUE(std::filesystem::exists(meta_path));

    const auto parsed = engine::parse_asset_meta(read_file(meta_path));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(engine::AssetId::is_valid(parsed->guid.hex()));
    EXPECT_EQ(parsed->importer, engine::ImporterKind::Ui);
    const std::string guid(parsed->guid.hex());

    EXPECT_EQ(engine::write_missing_metas(tree.path), 0);
    const auto again = engine::parse_asset_meta(read_file(meta_path));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->guid.hex(), guid);
    EXPECT_EQ(again->importer, engine::ImporterKind::Ui);
}

TEST(Assets, GetUiDocument) {
    TempTree tree;
    write_file(tree.path / "hud.xml", "<Canvas><Label Text=\"Hi\"/></Canvas>");

    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(tree.path);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kUiGuid}, "hud.xml", engine::ImporterKind::Ui});
    db.set_catalog(std::move(catalog));

    const auto document = db.Get<engine::ui::UiDocument>(engine::AssetId{kUiGuid});
    ASSERT_NE(document, nullptr);
    EXPECT_EQ(document->root.kind, engine::ui::ElementKind::Canvas);
    ASSERT_EQ(document->root.children.size(), 1u);
    EXPECT_EQ(document->root.children[0].kind, engine::ui::ElementKind::Label);
    EXPECT_EQ(document->root.children[0].text, "Hi");
}

TEST(Assets, GetStyleSheet) {
    TempTree tree;
    write_file(tree.path / "hud.css", ".hud { padding: 16; }\n");

    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(tree.path);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kCssGuid}, "hud.css", engine::ImporterKind::Css});
    db.set_catalog(std::move(catalog));

    const auto sheet = db.Get<engine::ui::Stylesheet>(engine::AssetId{kCssGuid});
    ASSERT_NE(sheet, nullptr);
    ASSERT_FALSE(sheet->rules.empty());
    EXPECT_EQ(sheet->rules[0].selector.class_name, "hud");
}

TEST(Assets, GetSoundFromCatalog) {
    TempTree tree;
    write_file(tree.path / "sfx" / "step.wav", "");

    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(tree.path);

    engine::CatalogEntry entry;
    entry.guid = engine::AssetId{kAudioGuid};
    entry.relative_path = "sfx/step.wav";
    entry.importer = engine::ImporterKind::Audio;
    entry.audio.volume = 0.5f;

    engine::CookedCatalog catalog;
    catalog.add(std::move(entry));
    db.set_catalog(std::move(catalog));

    const auto sound = db.Get<engine::Sound>(engine::AssetId{kAudioGuid});
    ASSERT_NE(sound, nullptr);
    EXPECT_FLOAT_EQ(sound->volume, 0.5f);
    ASSERT_NE(sound->clip, nullptr);
}

TEST(Assets, TextureNotReadyWithoutFactory) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kTextureGuid}, "textures/player.png", engine::ImporterKind::Texture});
    db.set_catalog(std::move(catalog));

    const auto result = db.TryGet<engine::render::ITexture>(engine::AssetId{kTextureGuid});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::AssetError::NotReady);
}
