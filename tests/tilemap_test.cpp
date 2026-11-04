#include <gtest/gtest.h>

#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>
#include <engine/render/primitives.h>
#include <engine/render/tilemap.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/meta.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "fixtures/fatal_fakes.h"

namespace {

using engine::render::Tilemap;
using engine::render::TilemapAsset;

constexpr const char* kGuid = "0123456789abcdef0123456789abcdef";

class SizedTexture final : public engine::render::ITexture {
public:
    SizedTexture(int width, int height) : w(width), h(height) {}
    int width() const noexcept override { return w; }
    int height() const noexcept override { return h; }
    int w;
    int h;
};

constexpr std::uint8_t kPng1x1Red[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
        0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x08,
        0x3E, 0x33, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

// Records each update the renderer makes; `fail_updates` makes the backend refuse them.
class FakeMesh : public engine::render::IMesh {
public:
    FakeMesh() : engine::render::IMesh(engine::render::make_quad()) {}
    explicit FakeMesh(const engine::render::MeshDesc& desc) : engine::render::IMesh(desc) {}

    int updates = 0;
    bool fail_updates = false;
    engine::render::MeshDesc last;

protected:
    bool replace(const engine::render::MeshDesc& desc) override {
        ++updates;
        if (fail_updates) {
            return false;
        }
        last = desc;
        return true;
    }
};

class CountingFactory : public engine::render::IGraphicFactory {
public:
    std::shared_ptr<engine::render::IMesh> create_mesh(const engine::render::MeshDesc& desc) override {
        ++meshes;
        if (fail_meshes) {
            return nullptr;
        }
        auto mesh = std::make_shared<FakeMesh>(desc);
        mesh->last = desc;
        return mesh;
    }
    std::shared_ptr<engine::render::IShader> create_shader(const engine::render::ShaderDesc&) override { return {}; }
    std::shared_ptr<engine::render::ITexture> create_texture(const engine::render::TextureDesc& desc) override {
        return std::make_shared<SizedTexture>(desc.width, desc.height);
    }
    std::shared_ptr<engine::render::ITexture> create_texture_array(const engine::render::TextureArrayDesc&) override {
        return {};
    }
    std::shared_ptr<engine::render::IDynamicTexture> create_dynamic_texture(
            const engine::render::DynamicTextureDesc&) override {
        return {};
    }
    int meshes = 0;
    bool fail_meshes = false;
};

// A 70 x 40 cell map (3 x 2 chunks) over a 64 x 32 px tileset of 16 px tiles (4 columns, 2 rows). Every cell is empty
// except what a test sets.
std::shared_ptr<TilemapAsset> make_asset(int width = 70, int height = 40) {
    auto asset = std::make_shared<TilemapAsset>();
    asset->desc.width = width;
    asset->desc.height = height;
    asset->desc.tile_size = {16, 16};
    asset->desc.columns = 4;
    asset->desc.pixels_per_unit = 16.0f;
    asset->desc.layers.push_back({"ground", std::vector<std::uint16_t>(static_cast<std::size_t>(width * height), 0)});
    asset->texture_size = {64, 32};
    return asset;
}

std::string toml_with_tiles(const std::string& tiles) {
    return std::string("texture = \"asset://") + kGuid + "\"\nsize = [2, 2]\ntile_size = [16, 16]\n[[layers]]\nname = \"a\"\n" +
            "tiles = " + tiles + "\n";
}

}

TEST(Tilemap, ParsesLayersAndSizes) {
    const auto desc = engine::render::parse_tilemap(toml_with_tiles("[0, 1, 2, 0]"));
    ASSERT_TRUE(desc.has_value());
    EXPECT_EQ(desc->width, 2);
    EXPECT_EQ(desc->height, 2);
    EXPECT_EQ(desc->tile_size, (glm::ivec2{16, 16}));
    EXPECT_EQ(desc->columns, 0);
    ASSERT_EQ(desc->layers.size(), 1u);
    EXPECT_EQ(desc->layers[0].name, "a");
    EXPECT_EQ(desc->layers[0].tiles, (std::vector<std::uint16_t>{0, 1, 2, 0}));
}

TEST(Tilemap, ParsesAnimatedTiles) {
    const std::string toml = toml_with_tiles("[0, 2, 0, 0]") +
            "[[animations]]\ntile = 2\nframes = 4\nfps = 8.0\n";
    const auto desc = engine::render::parse_tilemap(toml);
    ASSERT_TRUE(desc.has_value());
    ASSERT_EQ(desc->animations.size(), 1u);
    EXPECT_EQ(desc->animations[0].tile, 2);
    EXPECT_EQ(desc->animations[0].frames, 4);
    EXPECT_FLOAT_EQ(desc->animations[0].fps, 8.0f);
}

TEST(Tilemap, BakeChunkEncodesTileAnimation) {
    auto asset = make_asset();
    asset->desc.animations.push_back({.tile = 2, .frames = 4, .fps = 12.0f});
    asset->desc.layers[0].tiles[0] = 1;
    asset->desc.layers[0].tiles[1] = 2;

    const auto mesh = engine::render::bake_tilemap_chunk(*asset, asset->desc.layers[0].tiles, 0, 0);
    ASSERT_EQ(mesh.vertices.size(), 8u);

    EXPECT_FLOAT_EQ(mesh.vertices[0].normal.x, 0.0f);

    EXPECT_FLOAT_EQ(mesh.vertices[4].normal.x, 4.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[4].normal.y, 12.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[4].normal.z, 16.0f / 64.0f);
}

TEST(Tilemap, ParseRejectsWrongTileCountBadGuidAndNegativeTile) {
    EXPECT_FALSE(engine::render::parse_tilemap(toml_with_tiles("[0, 1, 2]")).has_value());
    EXPECT_FALSE(engine::render::parse_tilemap(toml_with_tiles("[0, 1, 2, -1]")).has_value());
    EXPECT_FALSE(engine::render::parse_tilemap("size = [2, 2]\ntile_size = [16, 16]\nlayers = []\n").has_value());
    EXPECT_FALSE(engine::render::parse_tilemap("not toml [").has_value());
    std::string bad_guid = toml_with_tiles("[0, 0, 0, 0]");
    bad_guid.replace(bad_guid.find(kGuid), 32, "zz");
    EXPECT_FALSE(engine::render::parse_tilemap(bad_guid).has_value());
}

TEST(Tilemap, BakeSkipsEmptyCellsAndBakesUvAndPosition) {
    auto asset = make_asset(2, 2);
    // Tile 6 is column 1, row 1 of the 4 x 2 tileset. It sits in cell (1, 0): second column, top row.
    asset->desc.layers[0].tiles = {0, 6, 0, 0};

    const auto mesh = engine::render::bake_tilemap_chunk(*asset, asset->desc.layers[0].tiles, 0, 0);
    ASSERT_EQ(mesh.vertices.size(), 4u);
    ASSERT_EQ(mesh.indices.size(), 6u);

    // Cell size is 1 world unit (16 px at 16 ppu). Cell (1, 0) spans x [1, 2], y [-1, 0].
    EXPECT_FLOAT_EQ(mesh.vertices[0].position.x, 1.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[0].position.y, -1.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[2].position.x, 2.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[2].position.y, 0.0f);

    // Pixel rect (16, 16)-(32, 32) of a 64 x 32 image; V runs up from the image bottom.
    EXPECT_FLOAT_EQ(mesh.vertices[0].uv.x, 16.0f / 64.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[2].uv.x, 32.0f / 64.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[0].uv.y, 0.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[2].uv.y, 0.5f);
}

TEST(Tilemap, BakeOfAnEmptyChunkHasNoVertices) {
    auto asset = make_asset();
    EXPECT_TRUE(engine::render::bake_tilemap_chunk(*asset, asset->desc.layers[0].tiles, 1, 1).vertices.empty());
}

TEST(Tilemap, ChunkCountsRoundUp) {
    const Tilemap map = Tilemap::from_asset(make_asset());
    EXPECT_EQ(map.chunks_x(), 3);
    EXPECT_EQ(map.chunks_y(), 2);
}

TEST(Tilemap, SetTileEditsOnlyInsideTheMapAndReportsChange) {
    Tilemap map = Tilemap::from_asset(make_asset());
    EXPECT_TRUE(map.set_tile(0, 3, 4, 2));
    EXPECT_EQ(map.tile(0, 3, 4), 2);
    EXPECT_FALSE(map.set_tile(0, 3, 4, 2));
    EXPECT_FALSE(map.set_tile(0, 70, 0, 1));
    EXPECT_FALSE(map.set_tile(0, 0, -1, 1));
    EXPECT_FALSE(map.set_tile(1, 0, 0, 1));
    EXPECT_EQ(map.tile(0, 70, 0), 0);
}

TEST(Tilemap, TileEditDoesNotTouchTheAsset) {
    auto asset = make_asset();
    Tilemap map = Tilemap::from_asset(asset);
    map.set_tile(0, 0, 0, 3);
    EXPECT_EQ(asset->desc.layers[0].tiles[0], 0);
}

TEST(Tilemap, ChunkVisibilityOrthographic) {
    const engine::render::Frustum frustum =
            engine::render::make_frustum(glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, -1.0f, 1.0f));
    const glm::mat4 identity{1.0f};
    const auto seen = [&](const glm::mat4& model, glm::vec2 min, glm::vec2 max) {
        const engine::render::Aabb box{.min = {min, 0.0f}, .max = {max, 0.0f}};
        return engine::render::frustum_intersects(frustum, engine::render::transform_aabb(box, model));
    };
    EXPECT_TRUE(seen(identity, {-1, -1}, {1, 1}));
    EXPECT_TRUE(seen(identity, {4, 4}, {9, 9}));
    EXPECT_FALSE(seen(identity, {6, -1}, {9, 1}));
    EXPECT_FALSE(seen(identity, {-1, -9}, {1, -6}));
    // The model matrix moves the box into view.
    EXPECT_TRUE(seen(glm::translate(identity, glm::vec3{-8.0f, 0.0f, 0.0f}), {6, -1}, {9, 1}));
}

TEST(Tilemap, OnlyVisibleChunksAreBuiltAndReturned) {
    auto asset = make_asset();
    Tilemap map = Tilemap::from_asset(asset);
    // One tile in chunk (0, 0) and one in chunk (2, 1), which is 64+ cells right and 32+ cells down.
    map.layers[0].tiles[0] = 1;
    map.layers[0].tiles[static_cast<std::size_t>(35 * 70 + 66)] = 1;

    CountingFactory factory;
    // The view covers x [0, 10], y [-10, 0]: only chunk (0, 0).
    const engine::render::Frustum view =
            engine::render::make_frustum(glm::ortho(0.0f, 10.0f, -10.0f, 0.0f, -1.0f, 1.0f));
    auto draws = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {&view, 1});
    ASSERT_EQ(draws.size(), 1u);
    EXPECT_EQ(factory.meshes, 1);

    // Same view again: the baked mesh is reused.
    draws = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {&view, 1});
    EXPECT_EQ(draws.size(), 1u);
    EXPECT_EQ(factory.meshes, 1);

    // No culling: both chunks that hold a tile; the empty ones have no mesh.
    draws = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {});
    EXPECT_EQ(draws.size(), 2u);
    EXPECT_EQ(factory.meshes, 2);
}

TEST(Tilemap, SetTileUpdatesOnlyItsChunkMesh) {
    Tilemap map = Tilemap::from_asset(make_asset());
    map.layers[0].tiles[0] = 1;
    map.layers[0].tiles[static_cast<std::size_t>(35 * 70 + 66)] = 1;

    CountingFactory factory;
    const auto before = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {});
    ASSERT_EQ(before.size(), 2u);
    ASSERT_EQ(factory.meshes, 2);

    ASSERT_TRUE(map.set_tile(0, 1, 0, 2));
    const auto after = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {});
    ASSERT_EQ(after.size(), 2u);
    EXPECT_EQ(factory.meshes, 2);
    EXPECT_EQ(after[0].mesh, before[0].mesh);
    EXPECT_EQ(after[1].mesh, before[1].mesh);

    const auto& edited = static_cast<const FakeMesh&>(*after[0].mesh);
    EXPECT_EQ(edited.updates, 1);
    const engine::render::MeshDesc baked =
            engine::render::bake_tilemap_chunk(*map.asset, map.layers[0].tiles, 0, 0);
    ASSERT_EQ(edited.last.vertices.size(), baked.vertices.size());
    EXPECT_EQ(edited.last.vertices.size(), 8u);
    EXPECT_EQ(edited.last.indices, baked.indices);
    for (std::size_t i = 0; i < baked.vertices.size(); ++i) {
        EXPECT_EQ(edited.last.vertices[i].position, baked.vertices[i].position);
        EXPECT_EQ(edited.last.vertices[i].uv, baked.vertices[i].uv);
    }
    EXPECT_EQ(static_cast<const FakeMesh&>(*after[1].mesh).updates, 0);
}

TEST(Tilemap, ClearingAChunkDropsItsMesh) {
    Tilemap map = Tilemap::from_asset(make_asset());
    map.layers[0].tiles[0] = 1;

    CountingFactory factory;
    const auto before = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {});
    ASSERT_EQ(before.size(), 1u);
    const auto& mesh = static_cast<const FakeMesh&>(*before[0].mesh);

    ASSERT_TRUE(map.set_tile(0, 0, 0, 0));
    EXPECT_TRUE(engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {}).empty());
    EXPECT_EQ(mesh.updates, 0);
    EXPECT_EQ(map.chunks[0].mesh, nullptr);
    EXPECT_EQ(factory.meshes, 1);
}

TEST(Tilemap, FailedChunkUpdateMakesANewMesh) {
    Tilemap map = Tilemap::from_asset(make_asset());
    map.layers[0].tiles[0] = 1;

    CountingFactory factory;
    const auto before = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {});
    ASSERT_EQ(before.size(), 1u);
    auto old_mesh = std::static_pointer_cast<FakeMesh>(before[0].mesh);
    old_mesh->fail_updates = true;

    ASSERT_TRUE(map.set_tile(0, 1, 0, 2));
    const auto after = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {});
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(old_mesh->updates, 1);
    EXPECT_EQ(factory.meshes, 2);
    EXPECT_NE(after[0].mesh, old_mesh);
    EXPECT_EQ(static_cast<const FakeMesh&>(*after[0].mesh).last.vertices.size(), 8u);
}

TEST(Tilemap, ChunkWhoseMeshFailedIsBuiltAgainAndKeepsAnOldMesh) {
    Tilemap map = Tilemap::from_asset(make_asset());
    map.layers[0].tiles[0] = 1;

    CountingFactory factory;
    factory.fail_meshes = true;
    EXPECT_TRUE(engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {}).empty());
    factory.fail_meshes = false;
    const auto built = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {});
    ASSERT_EQ(built.size(), 1u);

    auto old_mesh = std::static_pointer_cast<FakeMesh>(built[0].mesh);
    old_mesh->fail_updates = true;
    factory.fail_meshes = true;
    ASSERT_TRUE(map.set_tile(0, 1, 0, 2));
    const auto kept = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {});
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept[0].mesh, old_mesh);

    factory.fail_meshes = false;
    const auto rebuilt = engine::render::visible_tilemap_chunks(map, factory, glm::mat4{1.0f}, {});
    ASSERT_EQ(rebuilt.size(), 1u);
    EXPECT_NE(rebuilt[0].mesh, old_mesh);
}

TEST(Tilemap, AssetsDbLoadsATilemapWithItsTileset) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wind_tilemap_load";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream png(dir / "tiles.png", std::ios::binary);
        png.write(reinterpret_cast<const char*>(kPng1x1Red), sizeof(kPng1x1Red));
        std::ofstream map(dir / "level.tilemap");
        map << "texture = \"asset://" << kGuid << "\"\nsize = [2, 1]\ntile_size = [1, 1]\n"
            << "[[layers]]\nname = \"a\"\ntiles = [1, 0]\n";
    }
    const auto map_id = engine::AssetId{"fedcba9876543210fedcba9876543210"};
    engine::CookedCatalog catalog;
    catalog.add(engine::CatalogEntry{engine::AssetId{kGuid}, "tiles.png", engine::ImporterKind::Texture});
    catalog.add(engine::CatalogEntry{map_id, "level.tilemap", engine::ImporterKind::Tilemap});

    CountingFactory factory;
    fakes::QuietFatal fatal;
    engine::AssetsDb db(fatal);
    db.set_catalog(std::move(catalog));
    db.set_root(dir);
    db.set_graphic_factory(&factory);

    const auto asset = db.try_get<TilemapAsset>(map_id);
    ASSERT_TRUE(asset.has_value());
    EXPECT_EQ((*asset)->desc.columns, 1);
    EXPECT_EQ((*asset)->texture_size, (glm::ivec2{1, 1}));
    EXPECT_EQ((*asset)->desc.layers[0].tiles, (std::vector<std::uint16_t>{1, 0}));

    // A texture asset is not a tilemap.
    EXPECT_FALSE(db.try_get<TilemapAsset>(engine::AssetId{kGuid}).has_value());
    std::filesystem::remove_all(dir);
}
