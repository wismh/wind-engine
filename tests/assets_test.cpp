#include <gtest/gtest.h>

#include <engine/audio/sound.h>
#include <engine/builtin_ids.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/resources/asset_guid.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/font.h>
#include <engine/resources/meta.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

namespace {

constexpr std::string_view kTextureGuid = "a1b2c3d4e5f6789012345678901234ab";
constexpr std::string_view kAudioGuid = "b1c2d3e4f567890123456789012345ab";
constexpr std::string_view kUiGuid = "d1e2f3a4567890123456789012345abc";
constexpr std::string_view kCssGuid = "e1f2a3b4567890123456789012345abc";
constexpr std::string_view kUiImageGuid = "f1a2b3c4567890123456789012345abc";
constexpr std::string_view kFontGuid = "c1a1c2d3e4f5678901234567890abc07";

// 1x1 RGB red PNG; stb_image converts to RGBA when requested.
constexpr std::uint8_t kPng1x1Red[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
        0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x08,
        0x3E, 0x33, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};
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

void write_bytes(const std::filesystem::path& path, const std::uint8_t* data, std::size_t size) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

bool load_builtin_catalog(engine::AssetsDb& db) {
    const auto scanned = engine::codegen_scan(std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR});
    if (!scanned) {
        return false;
    }
    db.set_catalog(scanned->catalog);
    db.set_root(std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR});
    return true;
}

class DummyMesh final : public engine::render::IMesh {};
class DummyShader final : public engine::render::IShader {};
class DummyTexture final : public engine::render::ITexture {};

class FakeGraphicFactory final : public engine::render::IGraphicFactory {
public:
    int mesh_calls = 0;
    int shader_calls = 0;
    int texture_calls = 0;
    engine::render::MeshDesc last_mesh;
    engine::render::ShaderDesc last_shader;
    engine::render::TextureDesc last_texture;
    std::shared_ptr<engine::render::IMesh> last_mesh_obj;
    std::shared_ptr<engine::render::IShader> last_shader_obj;
    std::shared_ptr<engine::render::ITexture> last_texture_obj;

    std::shared_ptr<engine::render::IMesh> create_mesh(const engine::render::MeshDesc& desc) override {
        ++mesh_calls;
        last_mesh = desc;
        last_mesh_obj = std::make_shared<DummyMesh>();
        return last_mesh_obj;
    }

    std::shared_ptr<engine::render::IShader> create_shader(const engine::render::ShaderDesc& desc) override {
        ++shader_calls;
        last_shader = desc;
        last_shader_obj = std::make_shared<DummyShader>();
        return last_shader_obj;
    }

    std::shared_ptr<engine::render::ITexture> create_texture(const engine::render::TextureDesc& desc) override {
        ++texture_calls;
        last_texture = desc;
        last_texture_obj = std::make_shared<DummyTexture>();
        return last_texture_obj;
    }
};

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

TEST(Assets, CatalogListsFontAfterSetAndAdd) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);

    engine::CookedCatalog first;
    first.add({engine::builtin::font_ui, "fonts/ui.ttf", engine::ImporterKind::Font});
    db.set_catalog(std::move(first));

    engine::CookedCatalog extra;
    extra.add({engine::AssetId{kFontGuid}, "fonts/hud.ttf", engine::ImporterKind::Font});
    db.add_catalog(std::move(extra));

    const engine::CatalogEntry* builtin = db.catalog().find(engine::builtin::font_ui);
    ASSERT_NE(builtin, nullptr);
    EXPECT_EQ(builtin->importer, engine::ImporterKind::Font);
    EXPECT_EQ(builtin->relative_path, "fonts/ui.ttf");

    const engine::CatalogEntry* game_font = db.catalog().find(engine::AssetId{kFontGuid});
    ASSERT_NE(game_font, nullptr);
    EXPECT_EQ(game_font->importer, engine::ImporterKind::Font);
    EXPECT_EQ(game_font->relative_path, "fonts/hud.ttf");

    int font_count = 0;
    for (const engine::CatalogEntry& entry : db.catalog().entries()) {
        if (entry.importer == engine::ImporterKind::Font) {
            ++font_count;
        }
    }
    EXPECT_EQ(font_count, 2);
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

TEST(Assets, CookedCatalogTextureFieldsRoundTrip) {
    engine::CatalogEntry entry;
    entry.guid = engine::AssetId{kTextureGuid};
    entry.relative_path = "textures/player.png";
    entry.importer = engine::ImporterKind::Texture;
    entry.texture.color_space = engine::ColorSpace::Linear;
    entry.texture.filter = engine::FilterMode::Nearest;
    entry.texture.wrap = engine::WrapMode::Repeat;
    entry.texture.layout = engine::TextureLayout::Multiple;

    engine::CookedCatalog catalog;
    catalog.add(entry);
    const auto loaded = engine::parse_cooked_catalog(catalog.serialize());
    ASSERT_TRUE(loaded.has_value());
    const engine::CatalogEntry* again = loaded->find(entry.guid);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->relative_path, "textures/player.png");
    EXPECT_EQ(again->importer, engine::ImporterKind::Texture);
    EXPECT_EQ(again->texture.color_space, engine::ColorSpace::Linear);
    EXPECT_EQ(again->texture.filter, engine::FilterMode::Nearest);
    EXPECT_EQ(again->texture.wrap, engine::WrapMode::Repeat);
    EXPECT_EQ(again->texture.layout, engine::TextureLayout::Multiple);

    TempTree tree;
    write_file(tree.path / "textures" / "player.png", "png");
    write_file(tree.path / "textures" / "player.png.meta", R"(
guid = "a1b2c3d4e5f6789012345678901234ab"
importer = "texture"
color_space = "linear"
filter = "nearest"
wrap = "repeat"
layout = "multiple"
)");
    const auto scanned = engine::codegen_scan(tree.path);
    ASSERT_TRUE(scanned.has_value());
    const engine::CatalogEntry* cooked = scanned->catalog.find(engine::AssetId{kTextureGuid});
    ASSERT_NE(cooked, nullptr);
    EXPECT_EQ(cooked->texture.color_space, engine::ColorSpace::Linear);
    EXPECT_EQ(cooked->texture.filter, engine::FilterMode::Nearest);
    EXPECT_EQ(cooked->texture.wrap, engine::WrapMode::Repeat);
    EXPECT_EQ(cooked->texture.layout, engine::TextureLayout::Multiple);
}

TEST(Assets, LoadCatalogFromTempDir) {
    TempTree tree;
    write_file(tree.path / "hud.xml", "<Canvas><Label Text=\"Hi\"/></Canvas>");

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kUiGuid}, "hud.xml", engine::ImporterKind::Ui});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    const auto document = db.TryGet<engine::ui::UiDocument>(engine::AssetId{kUiGuid});
    ASSERT_TRUE(document.has_value());
    ASSERT_NE(*document, nullptr);
    EXPECT_EQ((*document)->root.kind, engine::ui::ElementKind::Canvas);
}

TEST(Assets, LoadCatalogMissingFileNoCrash) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);

    const auto missing_catalog =
            db.load_catalog(std::filesystem::path("wind_missing_catalog") / "catalog.toml",
                    std::filesystem::path("wind_missing_catalog"));
    ASSERT_FALSE(missing_catalog.has_value());
    EXPECT_EQ(missing_catalog.error(), engine::MetaError::Io);

    TempTree tree;
    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kUiGuid}, "hud.xml", engine::ImporterKind::Ui});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());
    const auto missing_asset = db.TryGet<engine::ui::UiDocument>(engine::AssetId{kUiGuid});
    ASSERT_FALSE(missing_asset.has_value());
    EXPECT_EQ(missing_asset.error(), engine::AssetError::Corrupt);
}

TEST(Assets, GetBuiltinQuadMesh) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    ASSERT_TRUE(load_builtin_catalog(db));

    FakeGraphicFactory factory;
    db.set_graphic_factory(&factory);

    const auto mesh = db.Get<engine::render::IMesh>(engine::builtin::mesh_quad);
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh, factory.last_mesh_obj);
    EXPECT_EQ(factory.mesh_calls, 1);
    ASSERT_EQ(factory.last_mesh.vertices.size(), 6u);
    EXPECT_FLOAT_EQ(factory.last_mesh.vertices[0].position.x, -0.5f);
    EXPECT_FLOAT_EQ(factory.last_mesh.vertices[0].position.y, 0.5f);
    EXPECT_FLOAT_EQ(factory.last_mesh.vertices[0].position.z, 0.0f);
    EXPECT_FLOAT_EQ(factory.last_mesh.vertices[0].uv.x, 0.0f);
    EXPECT_FLOAT_EQ(factory.last_mesh.vertices[0].uv.y, 1.0f);

    EXPECT_EQ(db.Get<engine::render::IMesh>(engine::builtin::mesh_quad), mesh);
    EXPECT_EQ(factory.mesh_calls, 1);
}

TEST(Assets, GetBuiltinUnlitShader) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    ASSERT_TRUE(load_builtin_catalog(db));

    FakeGraphicFactory factory;
    db.set_graphic_factory(&factory);

    const auto shader = db.Get<engine::render::IShader>(engine::builtin::shader_unlit);
    ASSERT_NE(shader, nullptr);
    EXPECT_EQ(shader, factory.last_shader_obj);
    EXPECT_NE(factory.last_shader.vertex_src.find("#version 330"), std::string::npos);
    EXPECT_NE(factory.last_shader.vertex_src.find("aPosition"), std::string::npos);
    EXPECT_NE(factory.last_shader.fragment_src.find("uTexture"), std::string::npos);
    EXPECT_NE(factory.last_shader.fragment_src.find("uColor"), std::string::npos);
}

TEST(Assets, GetBuiltinUnlitMaterial) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    ASSERT_TRUE(load_builtin_catalog(db));

    FakeGraphicFactory factory;
    db.set_graphic_factory(&factory);

    const auto material = db.Get<engine::render::IMaterial>(engine::builtin::material_unlit);
    ASSERT_NE(material, nullptr);
    ASSERT_NE(material->Shader(), nullptr);
    EXPECT_EQ(material->Shader(), factory.last_shader_obj);
    EXPECT_EQ(material->Texture(0), nullptr);
    EXPECT_EQ(material->Blend(), engine::render::BlendMode::Alpha);
    EXPECT_FLOAT_EQ(material->Color().r, 1.0f);
    EXPECT_FLOAT_EQ(material->Color().g, 1.0f);
    EXPECT_FLOAT_EQ(material->Color().b, 1.0f);
    EXPECT_FLOAT_EQ(material->Color().a, 1.0f);
}

TEST(Assets, MaterialNotReadyWithoutFactory) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    ASSERT_TRUE(load_builtin_catalog(db));

    const auto result = db.TryGet<engine::render::IMaterial>(engine::builtin::material_unlit);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::AssetError::NotReady);
}

TEST(Assets, GetTextureFromPng) {
    TempTree tree;
    write_bytes(tree.path / "textures" / "red.png", kPng1x1Red, sizeof(kPng1x1Red));

    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(tree.path);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kTextureGuid}, "textures/red.png", engine::ImporterKind::Texture});
    db.set_catalog(std::move(catalog));

    FakeGraphicFactory factory;
    db.set_graphic_factory(&factory);

    const auto texture = db.Get<engine::render::ITexture>(engine::AssetId{kTextureGuid});
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture, factory.last_texture_obj);
    EXPECT_EQ(factory.last_texture.width, 1);
    EXPECT_EQ(factory.last_texture.height, 1);
    ASSERT_EQ(factory.last_texture.rgba.size(), 4u);
    EXPECT_EQ(factory.last_texture.rgba[0], 255);
    EXPECT_EQ(factory.last_texture.rgba[1], 0);
    EXPECT_EQ(factory.last_texture.rgba[2], 0);
    EXPECT_EQ(factory.last_texture.rgba[3], 255);
}

TEST(Assets, GetTextureFromUiImage) {
    TempTree tree;
    write_bytes(tree.path / "ui" / "icon.png", kPng1x1Red, sizeof(kPng1x1Red));

    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(tree.path);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kUiImageGuid}, "ui/icon.png", engine::ImporterKind::UiImage});
    db.set_catalog(std::move(catalog));

    FakeGraphicFactory factory;
    db.set_graphic_factory(&factory);

    const auto texture = db.Get<engine::render::ITexture>(engine::AssetId{kUiImageGuid});
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(factory.texture_calls, 1);
    EXPECT_EQ(factory.last_texture.width, 1);
    EXPECT_EQ(factory.last_texture.height, 1);
}

TEST(Assets, GetMeshTypeMismatch) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    ASSERT_TRUE(load_builtin_catalog(db));

    FakeGraphicFactory factory;
    db.set_graphic_factory(&factory);

    const auto result = db.TryGet<engine::render::IMesh>(engine::builtin::shader_unlit);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::AssetError::TypeMismatch);
    EXPECT_EQ(factory.mesh_calls, 0);
}

TEST(Assets, GetFontWithoutFactory) {
    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    ASSERT_TRUE(load_builtin_catalog(db));

    const auto font = db.Get<engine::Font>(engine::builtin::font_ui);
    ASSERT_NE(font, nullptr);
    EXPECT_FALSE(font->bytes.empty());

    const auto again = db.TryGet<engine::Font>(engine::builtin::font_ui);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, font);
}

TEST(Assets, CorruptPngIsCorrupt) {
    TempTree tree;
    write_file(tree.path / "textures" / "bad.png", "not-a-png");

    SilentFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(tree.path);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kTextureGuid}, "textures/bad.png", engine::ImporterKind::Texture});
    db.set_catalog(std::move(catalog));

    FakeGraphicFactory factory;
    db.set_graphic_factory(&factory);

    const auto result = db.TryGet<engine::render::ITexture>(engine::AssetId{kTextureGuid});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::AssetError::Corrupt);
    EXPECT_EQ(factory.texture_calls, 0);
}
