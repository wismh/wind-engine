#include <gtest/gtest.h>

#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/primitives.h>
#include <engine/render/texture_array.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/image_io.h>
#include <engine/resources/meta.h>

#include "fixtures/fatal_fakes.h"
#include "render/texture_check.h"
#include "resources/importers.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kArrayGuid = "7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d";
constexpr std::string_view kMaterialGuid = "7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4e";
constexpr std::string_view kShaderGuid = "7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4f";
constexpr std::string_view kTextureGuid = "7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c50";

struct TempTree {
    std::filesystem::path path;

    TempTree() {
        static int seq = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
                ("wind_texarray_" + std::to_string(stamp) + "_" + std::to_string(++seq));
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

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A 4 x 2 image: pixel (x, y) is {x, y, 10 + x, 255}, top row first.
engine::render::TextureDesc gradient_4x2() {
    engine::render::TextureDesc image;
    image.width = 4;
    image.height = 2;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 4; ++x) {
            image.rgba.insert(image.rgba.end(), {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y),
                                                        static_cast<std::uint8_t>(10 + x), 255});
        }
    }
    return image;
}

engine::SpriteMeta frame(std::string name, int x, int y, int w, int h) {
    return engine::SpriteMeta{.name = std::move(name), .rect = {x, y, w, h}};
}

// Two 2 x 2 frames of the 4 x 2 image: "left" and "right".
constexpr std::string_view kArrayMeta = R"(
guid = "7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d"
importer = "texture_array"
filter = "nearest"
mipmaps = "linear"

[[sprites]]
name = "left"
rect = [0, 0, 2, 2]

[[sprites]]
name = "right"
rect = [2, 0, 2, 2]
)";

class LayeredTexture final : public engine::render::ITexture {
public:
    LayeredTexture(int width, int height, int layers) : width_(width), height_(height), layers_(layers) {}
    int width() const noexcept override { return width_; }
    int height() const noexcept override { return height_; }
    int layers() const noexcept override { return layers_; }

private:
    int width_ = 0;
    int height_ = 0;
    int layers_ = 0;
};

class NullMesh final : public engine::render::IMesh {
protected:
    bool replace(const engine::render::MeshDesc&) override { return true; }
};
class NullShader final : public engine::render::IShader {};

class RecordingFactory final : public engine::render::IGraphicFactory {
public:
    std::shared_ptr<engine::render::IMesh> create_mesh(const engine::render::MeshDesc&) override {
        return std::make_shared<NullMesh>();
    }
    std::shared_ptr<engine::render::IShader> create_shader(const engine::render::ShaderDesc&) override {
        return std::make_shared<NullShader>();
    }
    std::shared_ptr<engine::render::ITexture> create_texture(const engine::render::TextureDesc& desc) override {
        last_texture = desc;
        return std::make_shared<LayeredTexture>(desc.width, desc.height, 0);
    }
    std::shared_ptr<engine::render::ITexture> create_texture_array(
            const engine::render::TextureArrayDesc& desc) override {
        ++array_calls;
        last_array = desc;
        return std::make_shared<LayeredTexture>(desc.width, desc.height, desc.layers);
    }
    std::shared_ptr<engine::render::IDynamicTexture> create_dynamic_texture(
            const engine::render::DynamicTextureDesc&) override {
        return {};
    }

    int array_calls = 0;
    engine::render::TextureArrayDesc last_array;
    engine::render::TextureDesc last_texture;
};

// The 4 x 2 image as `textures/blocks.png` with the texture_array `.meta` cooked into the catalog.
struct ArrayAssets {
    TempTree tree;
    fakes::QuietFatal fatal;
    engine::AssetsDb db{fatal};
    RecordingFactory factory;

    explicit ArrayAssets(std::string_view meta_text = kArrayMeta) {
        write_bytes(tree.path / "textures" / "blocks.png", engine::encode_png_rgba(gradient_4x2()));
        const auto meta = engine::parse_asset_meta(meta_text);
        EXPECT_TRUE(meta.has_value());
        engine::CookedCatalog catalog;
        engine::CatalogEntry entry{engine::AssetId{kArrayGuid}, "textures/blocks.png", engine::ImporterKind::TextureArray};
        if (meta) {
            entry.texture = meta->texture;
        }
        catalog.add(std::move(entry));
        db.set_root(tree.path);
        db.set_catalog(std::move(catalog));
        db.set_graphic_factory(&factory);
    }
};

}

TEST(TextureCheck, MipLevelCount) {
    EXPECT_EQ(engine::render::mip_level_count(1, 1), 1);
    EXPECT_EQ(engine::render::mip_level_count(2, 2), 2);
    EXPECT_EQ(engine::render::mip_level_count(16, 16), 5);
    EXPECT_EQ(engine::render::mip_level_count(16, 4), 5);
    EXPECT_EQ(engine::render::mip_level_count(5, 3), 3);
    EXPECT_EQ(engine::render::mip_level_count(0, 0), 1);
}

TEST(TextureCheck, ArrayDescErrors) {
    engine::render::TextureArrayDesc desc{.width = 2, .height = 2, .layers = 3};
    desc.rgba.assign(2 * 2 * 4 * 3, 0);
    EXPECT_TRUE(engine::render::texture_array_desc_error(desc).empty());

    auto broken = desc;
    broken.width = 0;
    EXPECT_FALSE(engine::render::texture_array_desc_error(broken).empty());
    broken = desc;
    broken.height = -1;
    EXPECT_FALSE(engine::render::texture_array_desc_error(broken).empty());
    broken = desc;
    broken.layers = 0;
    EXPECT_FALSE(engine::render::texture_array_desc_error(broken).empty());
    broken = desc;
    broken.rgba.pop_back();
    EXPECT_FALSE(engine::render::texture_array_desc_error(broken).empty());
    broken = desc;
    broken.layers = engine::render::kMaxTextureArrayLayers + 1;
    broken.rgba.assign(2 * 2 * 4 * static_cast<std::size_t>(broken.layers), 0);
    EXPECT_FALSE(engine::render::texture_array_desc_error(broken).empty());
    broken.layers = engine::render::kMaxTextureArrayLayers;
    EXPECT_TRUE(engine::render::texture_array_desc_error(broken).empty());
}

TEST(TextureCheck, FlipLayersFlipsEachLayerOnItsOwn) {
    // Two 1 x 2 layers: rows {1, 2} and {3, 4}, each 4 bytes of one value.
    const std::vector<std::uint8_t> rgba = {1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4};
    std::vector<std::uint8_t> out;
    ASSERT_TRUE(engine::render::flip_layers_vertically_into(rgba, 1, 2, 2, out));
    EXPECT_EQ(out, (std::vector<std::uint8_t>{2, 2, 2, 2, 1, 1, 1, 1, 4, 4, 4, 4, 3, 3, 3, 3}));

    std::vector<std::uint8_t> untouched{9};
    EXPECT_FALSE(engine::render::flip_layers_vertically_into(rgba, 1, 2, 3, untouched));
    EXPECT_FALSE(engine::render::flip_layers_vertically_into(rgba, 0, 2, 2, untouched));
    EXPECT_EQ(untouched, std::vector<std::uint8_t>{9});
}

TEST(TextureArrayImport, CopiesFramesInSpriteOrder) {
    const std::vector<engine::SpriteMeta> frames = {frame("right", 2, 0, 2, 2), frame("left", 0, 0, 2, 2)};
    const auto desc = engine::texture_array_from_frames(gradient_4x2(), frames);
    ASSERT_TRUE(desc.has_value()) << desc.error();
    EXPECT_EQ(desc->width, 2);
    EXPECT_EQ(desc->height, 2);
    EXPECT_EQ(desc->layers, 2);
    ASSERT_EQ(desc->rgba.size(), 2u * 2u * 4u * 2u);
    // Layer 0 is "right": its top-left pixel is image pixel (2, 0); its bottom-right is (3, 1).
    EXPECT_EQ(std::vector<std::uint8_t>(desc->rgba.begin(), desc->rgba.begin() + 4),
            (std::vector<std::uint8_t>{2, 0, 12, 255}));
    EXPECT_EQ(std::vector<std::uint8_t>(desc->rgba.begin() + 12, desc->rgba.begin() + 16),
            (std::vector<std::uint8_t>{3, 1, 13, 255}));
    // Layer 1 is "left": top-left (0, 0), second row first pixel (0, 1).
    EXPECT_EQ(std::vector<std::uint8_t>(desc->rgba.begin() + 16, desc->rgba.begin() + 20),
            (std::vector<std::uint8_t>{0, 0, 10, 255}));
    EXPECT_EQ(std::vector<std::uint8_t>(desc->rgba.begin() + 24, desc->rgba.begin() + 28),
            (std::vector<std::uint8_t>{0, 1, 10, 255}));
    EXPECT_EQ(desc->mipmaps, engine::MipmapMode::None);
}

TEST(TextureArrayImport, RejectsBadFrames) {
    const engine::render::TextureDesc image = gradient_4x2();
    EXPECT_FALSE(engine::texture_array_from_frames(image, {}).has_value());

    const std::vector<engine::SpriteMeta> outside = {frame("a", 3, 0, 2, 2)};
    EXPECT_FALSE(engine::texture_array_from_frames(image, outside).has_value());
    const std::vector<engine::SpriteMeta> below = {frame("a", 0, 1, 2, 2)};
    EXPECT_FALSE(engine::texture_array_from_frames(image, below).has_value());
    const std::vector<engine::SpriteMeta> negative = {frame("a", -1, 0, 2, 2)};
    EXPECT_FALSE(engine::texture_array_from_frames(image, negative).has_value());

    const std::vector<engine::SpriteMeta> mixed = {frame("a", 0, 0, 2, 2), frame("b", 2, 0, 1, 1)};
    const auto error = engine::texture_array_from_frames(image, mixed);
    ASSERT_FALSE(error.has_value());
    EXPECT_NE(error.error().find("\"b\""), std::string::npos) << error.error();

    std::vector<engine::SpriteMeta> too_many(engine::render::kMaxTextureArrayLayers + 1, frame("a", 0, 0, 1, 1));
    EXPECT_FALSE(engine::texture_array_from_frames(image, too_many).has_value());
}

TEST(TextureArrayMeta, MipmapsParseAndDefaultToNone) {
    const std::string head = "guid = \"7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c50\"\nimporter = \"texture\"\n";
    const auto none = engine::parse_asset_meta(head);
    ASSERT_TRUE(none.has_value());
    EXPECT_EQ(none->texture.mipmaps, engine::MipmapMode::None);
    EXPECT_EQ(engine::parse_asset_meta(head + "mipmaps = \"none\"\n")->texture.mipmaps, engine::MipmapMode::None);
    EXPECT_EQ(engine::parse_asset_meta(head + "mipmaps = \"nearest\"\n")->texture.mipmaps,
            engine::MipmapMode::Nearest);
    EXPECT_EQ(engine::parse_asset_meta(head + "mipmaps = \"linear\"\n")->texture.mipmaps, engine::MipmapMode::Linear);
    EXPECT_EQ(engine::parse_asset_meta(head + "mipmaps = \"trilinear\"\n").error(), engine::MetaError::InvalidField);
    EXPECT_EQ(engine::parse_asset_meta(head + "mipmaps = true\n").error(), engine::MetaError::InvalidField);
}

TEST(TextureArrayMeta, ParsesFramesAsLayers) {
    const auto meta = engine::parse_asset_meta(kArrayMeta);
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->importer, engine::ImporterKind::TextureArray);
    EXPECT_EQ(engine::to_string(meta->importer), "texture_array");
    EXPECT_EQ(meta->texture.filter, engine::FilterMode::Nearest);
    EXPECT_EQ(meta->texture.mipmaps, engine::MipmapMode::Linear);
    ASSERT_EQ(meta->texture.sprites.size(), 2u);
    EXPECT_EQ(meta->texture.sprites[1].name, "right");
}

TEST(TextureArrayMeta, NeedsEqualFramesWithUniqueNames) {
    const std::string head = "guid = \"7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d\"\nimporter = \"texture_array\"\n";
    const auto sprite = [](std::string_view name, int w) {
        return std::string("[[sprites]]\nname = \"") + std::string(name) + "\"\nrect = [0, 0, " + std::to_string(w) +
                ", 2]\n";
    };
    EXPECT_EQ(engine::parse_asset_meta(head).error(), engine::MetaError::InvalidField);
    EXPECT_EQ(engine::parse_asset_meta(head + sprite("a", 2) + sprite("b", 3)).error(),
            engine::MetaError::InvalidField);
    EXPECT_EQ(engine::parse_asset_meta(head + sprite("a", 2) + sprite("a", 2)).error(),
            engine::MetaError::InvalidField);
    EXPECT_TRUE(engine::parse_asset_meta(head + sprite("a", 2) + sprite("b", 2)).has_value());

    std::string many = head;
    for (int i = 0; i <= engine::render::kMaxTextureArrayLayers; ++i) {
        many += sprite("f" + std::to_string(i), 1);
    }
    EXPECT_EQ(engine::parse_asset_meta(many).error(), engine::MetaError::InvalidField);
}

TEST(TextureArrayMeta, CatalogRoundTripsMipmapsAndTextureArray) {
    engine::CookedCatalog catalog;
    engine::CatalogEntry array{engine::AssetId{kArrayGuid}, "textures/blocks.png", engine::ImporterKind::TextureArray};
    array.texture = engine::parse_asset_meta(kArrayMeta)->texture;
    catalog.add(std::move(array));
    engine::CatalogEntry texture{engine::AssetId{kTextureGuid}, "textures/x.png", engine::ImporterKind::Texture};
    texture.texture.mipmaps = engine::MipmapMode::Nearest;
    catalog.add(std::move(texture));

    const auto loaded = engine::parse_cooked_catalog(catalog.serialize());
    ASSERT_TRUE(loaded.has_value());
    const engine::CatalogEntry* found = loaded->find(engine::AssetId{kArrayGuid});
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->importer, engine::ImporterKind::TextureArray);
    EXPECT_EQ(found->texture.mipmaps, engine::MipmapMode::Linear);
    EXPECT_EQ(found->texture.filter, engine::FilterMode::Nearest);
    ASSERT_EQ(found->texture.sprites.size(), 2u);
    EXPECT_EQ(found->texture.sprites[0].name, "left");
    EXPECT_EQ(found->texture.sprites[1].rect, (engine::SpriteRect{2, 0, 2, 2}));
    EXPECT_EQ(loaded->find(engine::AssetId{kTextureGuid})->texture.mipmaps, engine::MipmapMode::Nearest);
}

TEST(TextureArrayAssets, LayersAreAddressedByFrameName) {
    ArrayAssets assets;
    const auto array = assets.db.get<engine::render::TextureArray>(engine::AssetId{kArrayGuid});
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array->layer("left"), 0);
    EXPECT_EQ(array->layer("right"), 1);
    EXPECT_EQ(array->layer("missing"), std::nullopt);
    ASSERT_NE(array->texture, nullptr);
    EXPECT_EQ(array->texture->layers(), 2);
    EXPECT_EQ(array->texture->width(), 2);
    EXPECT_EQ(array->texture, assets.db.get<engine::render::ITexture>(engine::AssetId{kArrayGuid}));
    EXPECT_EQ(assets.factory.array_calls, 1);
    EXPECT_EQ(assets.factory.last_array.layers, 2);
    EXPECT_EQ(assets.factory.last_array.filter, engine::FilterMode::Nearest);
    EXPECT_EQ(assets.factory.last_array.mipmaps, engine::MipmapMode::Linear);
    // The second layer's first pixel is image pixel (2, 0).
    EXPECT_EQ(assets.factory.last_array.rgba[16], 2);
    EXPECT_EQ(assets.factory.last_array.rgba[18], 12);
}

TEST(TextureArrayAssets, IsNotASpriteSheet) {
    ArrayAssets assets;
    EXPECT_EQ(assets.db.try_get_sprite(engine::AssetId{kArrayGuid}, "left").error(), engine::AssetError::TypeMismatch);
    EXPECT_EQ(assets.db.try_get_sprite(engine::AssetId{kArrayGuid}).error(), engine::AssetError::TypeMismatch);
    EXPECT_EQ(assets.db.try_get<engine::render::TextureDesc>(engine::AssetId{kArrayGuid}).error(),
            engine::AssetError::TypeMismatch);
}

TEST(TextureArrayAssets, NotReadyWithoutFactory) {
    ArrayAssets assets;
    assets.db.set_graphic_factory(nullptr);
    EXPECT_EQ(assets.db.try_get<engine::render::TextureArray>(engine::AssetId{kArrayGuid}).error(),
            engine::AssetError::NotReady);
}

TEST(TextureArrayAssets, FrameOutsideTheImageIsCorrupt) {
    ArrayAssets assets(R"(
guid = "7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d"
importer = "texture_array"

[[sprites]]
name = "far"
rect = [3, 0, 2, 2]
)");
    EXPECT_EQ(assets.db.try_get<engine::render::TextureArray>(engine::AssetId{kArrayGuid}).error(),
            engine::AssetError::Corrupt);
    EXPECT_EQ(assets.factory.array_calls, 0);
}

TEST(TextureArrayAssets, MaterialAlbedoCanBeATextureArray) {
    ArrayAssets assets;
    write_text(assets.tree.path / "shaders" / "a.shader", "<shader><vertex>v</vertex><fragment>f</fragment></shader>");
    write_text(assets.tree.path / "materials" / "blocks.mat",
            std::string("shader = \"asset://") + std::string(kShaderGuid) + "\"\n[textures]\nalbedo = \"asset://" +
                    std::string(kArrayGuid) + "\"\n");
    engine::CookedCatalog extra;
    extra.add({engine::AssetId{kShaderGuid}, "shaders/a.shader", engine::ImporterKind::Shader});
    extra.add({engine::AssetId{kMaterialGuid}, "materials/blocks.mat", engine::ImporterKind::Material});
    assets.db.add_catalog(std::move(extra));

    const auto material = assets.db.get<engine::render::IMaterial>(engine::AssetId{kMaterialGuid});
    ASSERT_NE(material, nullptr);
    ASSERT_NE(material->texture(0), nullptr);
    EXPECT_EQ(material->texture(0)->layers(), 2);
    EXPECT_EQ(material->texture(0), assets.db.get<engine::render::TextureArray>(engine::AssetId{kArrayGuid})->texture);
}

TEST(TextureArrayAssets, TextureMipmapsReachTheFactory) {
    TempTree tree;
    write_bytes(tree.path / "x.png", engine::encode_png_rgba(gradient_4x2()));
    fakes::QuietFatal fatal;
    engine::AssetsDb db(fatal);
    engine::CookedCatalog catalog;
    engine::CatalogEntry entry{engine::AssetId{kTextureGuid}, "x.png", engine::ImporterKind::Texture};
    entry.texture.mipmaps = engine::MipmapMode::Linear;
    catalog.add(std::move(entry));
    db.set_root(tree.path);
    db.set_catalog(std::move(catalog));
    RecordingFactory factory;
    db.set_graphic_factory(&factory);

    const auto texture = db.get<engine::render::ITexture>(engine::AssetId{kTextureGuid});
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->layers(), 0);
    EXPECT_EQ(factory.last_texture.mipmaps, engine::MipmapMode::Linear);
    EXPECT_EQ(factory.array_calls, 0);
}

TEST(TextureArrayCodegen, FrameOutsideTheImageFailsTheCook) {
    TempTree tree;
    write_bytes(tree.path / "blocks.png", engine::encode_png_rgba(gradient_4x2()));
    write_text(tree.path / "blocks.png.meta", R"(
guid = "7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d"
importer = "texture_array"

[[sprites]]
name = "far"
rect = [3, 0, 2, 2]
)");
    const auto result = engine::codegen_scan(tree.path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, engine::CodegenErrorKind::TextureArray);
    EXPECT_NE(result.error().message.find("far"), std::string::npos) << result.error().message;
}

TEST(TextureArrayCodegen, MixedFrameSizesAreAnInvalidMeta) {
    TempTree tree;
    write_bytes(tree.path / "blocks.png", engine::encode_png_rgba(gradient_4x2()));
    write_text(tree.path / "blocks.png.meta", R"(
guid = "7a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d"
importer = "texture_array"

[[sprites]]
name = "a"
rect = [0, 0, 2, 2]

[[sprites]]
name = "b"
rect = [2, 0, 1, 1]
)");
    const auto result = engine::codegen_scan(tree.path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, engine::CodegenErrorKind::InvalidMeta);
}

TEST(TextureArrayCodegen, ValidArrayIsCooked) {
    TempTree tree;
    write_bytes(tree.path / "blocks.png", engine::encode_png_rgba(gradient_4x2()));
    write_text(tree.path / "blocks.png.meta", kArrayMeta);
    const auto result = engine::codegen_scan(tree.path);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    const engine::CatalogEntry* entry = result->catalog.find(engine::AssetId{kArrayGuid});
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->importer, engine::ImporterKind::TextureArray);
    EXPECT_EQ(entry->texture.sprites.size(), 2u);
}

TEST(TextureArrayMesh, VertexLayerDefaultsToZero) {
    const engine::render::MeshVertex vertex;
    EXPECT_EQ(vertex.layer, 0.0f);
    for (const engine::render::MeshVertex& v : engine::render::make_cube().vertices) {
        EXPECT_EQ(v.layer, 0.0f);
    }
    const auto parsed = engine::parse_mesh("0 0 0 0 0\n1 0 0 1 0\n0 1 0 0 1\n");
    ASSERT_TRUE(parsed.has_value());
    for (const engine::render::MeshVertex& v : parsed->vertices) {
        EXPECT_EQ(v.layer, 0.0f);
    }
}
