#pragma once

// docs/tech/modules/Render.md

#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>
#include <engine/resources/asset_id.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

// Cells per chunk side. A chunk is one baked mesh and one draw.
inline constexpr int kTilemapChunkSize = 32;

// One layer of cells, row-major, row 0 at the top. 0 is an empty cell; n > 0 is tile n - 1 of the tileset (left to
// right, top to bottom).
struct TilemapLayerDesc {
    std::string name;
    std::vector<std::uint16_t> tiles;
};

// The `.tilemap` file: the tileset (one texture cut into a grid) and the cell layers.
struct TilemapDesc {
    AssetId texture;
    int width = 0;
    int height = 0;
    glm::ivec2 tile_size{16, 16};
    // Tiles per tileset row. 0: the texture width divided by `tile_size.x`, resolved when the asset loads.
    int columns = 0;
    float pixels_per_unit = 100.0f;
    std::vector<TilemapLayerDesc> layers;
};

// Null when the text is not TOML, `texture` is not a GUID, a size is not positive, or a layer's `tiles` is not
// `width * height` long.
[[nodiscard]] std::optional<TilemapDesc> parse_tilemap(std::string_view toml_text);

// A loaded `.tilemap`: the description with `columns` resolved, and the tileset texture.
struct TilemapAsset {
    TilemapDesc desc;
    std::shared_ptr<ITexture> texture;
    glm::ivec2 texture_size{0, 0};

    // One cell in world units.
    [[nodiscard]] glm::vec2 cell_size() const {
        return glm::vec2{desc.tile_size} / desc.pixels_per_unit;
    }
};

// The mesh of chunk (`chunk_x`, `chunk_y`) of one layer, in the tilemap's local space: cell (0, 0) is the top-left
// corner at the origin, x grows right and y grows up, so row r spans y in [-(r + 1), -r] cell heights. One quad per
// non-empty cell with the tile's UV region baked in. Empty `vertices` when the chunk has no tile. CPU only.
[[nodiscard]] MeshDesc bake_tilemap_chunk(
        const TilemapAsset& asset, std::span<const std::uint16_t> tiles, int chunk_x, int chunk_y);

// True when the chunk's box (`min`, `max`, local XY) under `model` touches the clip volume of `view_projection`.
[[nodiscard]] bool tilemap_chunk_visible(
        const glm::mat4& model, glm::vec2 min, glm::vec2 max, const glm::mat4& view_projection);

struct Tilemap {
    std::shared_ptr<TilemapAsset> asset;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    int layer = 0;
    // Layer i of the map draws at `order_in_layer + i`, so later layers sit on top.
    int order_in_layer = 0;
    // The cells this component draws. `from_asset` copies them from the asset; `set_tile` edits them.
    std::vector<TilemapLayerDesc> layers;

    struct Chunk {
        std::shared_ptr<IMesh> mesh;
        bool built = false;
    };
    // Written by the renderer: layer-major, then row-major chunks. Rebuilt when a chunk is dirty.
    std::vector<Chunk> chunks;

    [[nodiscard]] static Tilemap from_asset(std::shared_ptr<TilemapAsset> asset);

    // 0 outside the map or for an unknown layer.
    [[nodiscard]] std::uint16_t tile(int layer_index, int x, int y) const;
    // False, and nothing changes, outside the map, for an unknown layer, or when the cell already holds `tile`.
    // Otherwise only the chunk that holds the cell is rebuilt.
    bool set_tile(int layer_index, int x, int y, std::uint16_t tile);

    [[nodiscard]] int chunks_x() const;
    [[nodiscard]] int chunks_y() const;
};

struct TilemapChunkDraw {
    int layer_index = 0;
    std::shared_ptr<IMesh> mesh;
};

// The meshes of the chunks that touch at least one of `view_projections`; an empty span skips culling and returns every
// chunk. Builds missing or dirty chunk meshes through `factory`, only for chunks it returns.
[[nodiscard]] std::vector<TilemapChunkDraw> visible_tilemap_chunks(Tilemap& map, IGraphicFactory& factory,
        const glm::mat4& model, std::span<const glm::mat4> view_projections);

}
