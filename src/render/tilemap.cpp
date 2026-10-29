#include <engine/render/tilemap.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace engine::render {
namespace {

std::optional<glm::ivec2> read_ivec2(const toml::node* node) {
    const toml::array* const arr = node == nullptr ? nullptr : node->as_array();
    if (arr == nullptr || arr->size() != 2) {
        return std::nullopt;
    }
    const auto x = arr->get(0)->value<std::int64_t>();
    const auto y = arr->get(1)->value<std::int64_t>();
    if (!x || !y) {
        return std::nullopt;
    }
    return glm::ivec2{static_cast<int>(*x), static_cast<int>(*y)};
}

int ceil_div(int a, int b) {
    return (a + b - 1) / b;
}

}

std::optional<TilemapDesc> parse_tilemap(std::string_view toml_text) {
    try {
        const toml::table table = toml::parse(toml_text);
        TilemapDesc desc;

        const auto texture = table["texture"].value<std::string>();
        if (!texture) {
            return std::nullopt;
        }
        const auto texture_id = AssetId::parse(*texture);
        if (!texture_id) {
            return std::nullopt;
        }
        desc.texture = *texture_id;

        const auto size = read_ivec2(table.get("size"));
        if (!size || size->x <= 0 || size->y <= 0) {
            return std::nullopt;
        }
        desc.width = size->x;
        desc.height = size->y;

        const auto tile_size = read_ivec2(table.get("tile_size"));
        if (!tile_size || tile_size->x <= 0 || tile_size->y <= 0) {
            return std::nullopt;
        }
        desc.tile_size = *tile_size;

        if (const toml::node* node = table.get("columns")) {
            const auto columns = node->value<std::int64_t>();
            if (!columns || *columns < 0) {
                return std::nullopt;
            }
            desc.columns = static_cast<int>(*columns);
        }
        if (const toml::node* node = table.get("pixels_per_unit")) {
            const auto ppu = node->value<double>();
            if (!ppu || *ppu <= 0.0) {
                return std::nullopt;
            }
            desc.pixels_per_unit = static_cast<float>(*ppu);
        }

        const toml::node* layers_node = table.get("layers");
        const toml::array* const layers = layers_node == nullptr ? nullptr : layers_node->as_array();
        if (layers == nullptr) {
            return std::nullopt;
        }
        const std::size_t cells = static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height);
        for (const toml::node& layer_node : *layers) {
            const toml::table* const layer_table = layer_node.as_table();
            if (layer_table == nullptr) {
                return std::nullopt;
            }
            TilemapLayerDesc layer;
            if (const auto name = (*layer_table)["name"].value<std::string>()) {
                layer.name = *name;
            }
            const toml::node* tiles_node = layer_table->get("tiles");
            const toml::array* const tiles = tiles_node == nullptr ? nullptr : tiles_node->as_array();
            if (tiles == nullptr || tiles->size() != cells) {
                return std::nullopt;
            }
            layer.tiles.reserve(cells);
            for (const toml::node& tile : *tiles) {
                const auto value = tile.value<std::int64_t>();
                if (!value || *value < 0 || *value > 0xFFFF) {
                    return std::nullopt;
                }
                layer.tiles.push_back(static_cast<std::uint16_t>(*value));
            }
            desc.layers.push_back(std::move(layer));
        }
        return desc;
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }
}

MeshDesc bake_tilemap_chunk(
        const TilemapAsset& asset, std::span<const std::uint16_t> tiles, int chunk_x, int chunk_y) {
    MeshDesc mesh;
    const TilemapDesc& desc = asset.desc;
    if (desc.columns <= 0 || asset.texture_size.x <= 0 || asset.texture_size.y <= 0 ||
            tiles.size() != static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height)) {
        return mesh;
    }
    const glm::vec2 cell = asset.cell_size();
    const glm::vec2 texture_size{asset.texture_size};
    const int x_begin = chunk_x * kTilemapChunkSize;
    const int y_begin = chunk_y * kTilemapChunkSize;
    const int x_end = std::min(x_begin + kTilemapChunkSize, desc.width);
    const int y_end = std::min(y_begin + kTilemapChunkSize, desc.height);

    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const std::uint16_t tile = tiles[static_cast<std::size_t>(y) * static_cast<std::size_t>(desc.width) +
                    static_cast<std::size_t>(x)];
            if (tile == 0) {
                continue;
            }
            const int index = tile - 1;
            const glm::vec2 pixel{
                    static_cast<float>((index % desc.columns) * desc.tile_size.x),
                    static_cast<float>((index / desc.columns) * desc.tile_size.y),
            };
            // Image rows run down, V runs up: the tile's top edge has the larger V (see SpriteSheet::get).
            const float u0 = pixel.x / texture_size.x;
            const float u1 = (pixel.x + static_cast<float>(desc.tile_size.x)) / texture_size.x;
            const float v1 = 1.0f - pixel.y / texture_size.y;
            const float v0 = 1.0f - (pixel.y + static_cast<float>(desc.tile_size.y)) / texture_size.y;

            const float x0 = static_cast<float>(x) * cell.x;
            const float x1 = x0 + cell.x;
            const float y1 = -static_cast<float>(y) * cell.y;
            const float y0 = y1 - cell.y;

            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(MeshVertex{.position = {x0, y0, 0.0f}, .uv = {u0, v0}});
            mesh.vertices.push_back(MeshVertex{.position = {x1, y0, 0.0f}, .uv = {u1, v0}});
            mesh.vertices.push_back(MeshVertex{.position = {x1, y1, 0.0f}, .uv = {u1, v1}});
            mesh.vertices.push_back(MeshVertex{.position = {x0, y1, 0.0f}, .uv = {u0, v1}});
            for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
                mesh.indices.push_back(base + i);
            }
        }
    }
    return mesh;
}

bool tilemap_chunk_visible(
        const glm::mat4& model, glm::vec2 min, glm::vec2 max, const glm::mat4& view_projection) {
    const glm::mat4 clip_from_local = view_projection * model;
    const std::array<glm::vec2, 4> corners{
            glm::vec2{min.x, min.y}, glm::vec2{max.x, min.y}, glm::vec2{max.x, max.y}, glm::vec2{min.x, max.y}};
    // Outside when every corner is beyond the same side plane.
    bool left = true;
    bool right = true;
    bool below = true;
    bool above = true;
    for (const glm::vec2 corner : corners) {
        const glm::vec4 clip = clip_from_local * glm::vec4{corner, 0.0f, 1.0f};
        left = left && clip.x < -clip.w;
        right = right && clip.x > clip.w;
        below = below && clip.y < -clip.w;
        above = above && clip.y > clip.w;
    }
    return !(left || right || below || above);
}

Tilemap Tilemap::from_asset(std::shared_ptr<TilemapAsset> asset) {
    Tilemap map;
    if (asset != nullptr) {
        map.layers = asset->desc.layers;
    }
    map.asset = std::move(asset);
    return map;
}

int Tilemap::chunks_x() const {
    return asset == nullptr ? 0 : ceil_div(asset->desc.width, kTilemapChunkSize);
}

int Tilemap::chunks_y() const {
    return asset == nullptr ? 0 : ceil_div(asset->desc.height, kTilemapChunkSize);
}

std::uint16_t Tilemap::tile(int layer_index, int x, int y) const {
    if (asset == nullptr || layer_index < 0 || layer_index >= static_cast<int>(layers.size()) || x < 0 || y < 0 ||
            x >= asset->desc.width || y >= asset->desc.height) {
        return 0;
    }
    const auto& tiles = layers[static_cast<std::size_t>(layer_index)].tiles;
    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(asset->desc.width) +
            static_cast<std::size_t>(x);
    return index < tiles.size() ? tiles[index] : std::uint16_t{0};
}

bool Tilemap::set_tile(int layer_index, int x, int y, std::uint16_t value) {
    if (asset == nullptr || layer_index < 0 || layer_index >= static_cast<int>(layers.size()) || x < 0 || y < 0 ||
            x >= asset->desc.width || y >= asset->desc.height) {
        return false;
    }
    auto& tiles = layers[static_cast<std::size_t>(layer_index)].tiles;
    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(asset->desc.width) +
            static_cast<std::size_t>(x);
    if (index >= tiles.size() || tiles[index] == value) {
        return false;
    }
    tiles[index] = value;
    const std::size_t per_layer = static_cast<std::size_t>(chunks_x()) * static_cast<std::size_t>(chunks_y());
    const std::size_t chunk = static_cast<std::size_t>(layer_index) * per_layer +
            static_cast<std::size_t>(y / kTilemapChunkSize) * static_cast<std::size_t>(chunks_x()) +
            static_cast<std::size_t>(x / kTilemapChunkSize);
    if (chunk < chunks.size()) {
        chunks[chunk].built = false;
    }
    return true;
}

std::vector<TilemapChunkDraw> visible_tilemap_chunks(Tilemap& map, IGraphicFactory& factory, const glm::mat4& model,
        std::span<const glm::mat4> view_projections) {
    std::vector<TilemapChunkDraw> draws;
    if (map.asset == nullptr) {
        return draws;
    }
    const int cx_count = map.chunks_x();
    const int cy_count = map.chunks_y();
    const std::size_t per_layer = static_cast<std::size_t>(cx_count) * static_cast<std::size_t>(cy_count);
    map.chunks.resize(per_layer * map.layers.size());

    const glm::vec2 cell = map.asset->cell_size();
    for (std::size_t layer = 0; layer < map.layers.size(); ++layer) {
        for (int cy = 0; cy < cy_count; ++cy) {
            for (int cx = 0; cx < cx_count; ++cx) {
                if (!view_projections.empty()) {
                    const int x0 = cx * kTilemapChunkSize;
                    const int y0 = cy * kTilemapChunkSize;
                    const int x1 = std::min(x0 + kTilemapChunkSize, map.asset->desc.width);
                    const int y1 = std::min(y0 + kTilemapChunkSize, map.asset->desc.height);
                    const glm::vec2 min{static_cast<float>(x0) * cell.x, -static_cast<float>(y1) * cell.y};
                    const glm::vec2 max{static_cast<float>(x1) * cell.x, -static_cast<float>(y0) * cell.y};
                    const bool seen = std::any_of(view_projections.begin(), view_projections.end(),
                            [&](const glm::mat4& vp) { return tilemap_chunk_visible(model, min, max, vp); });
                    if (!seen) {
                        continue;
                    }
                }
                Tilemap::Chunk& chunk =
                        map.chunks[layer * per_layer + static_cast<std::size_t>(cy * cx_count + cx)];
                if (!chunk.built) {
                    const MeshDesc mesh = bake_tilemap_chunk(*map.asset, map.layers[layer].tiles, cx, cy);
                    chunk.mesh = mesh.vertices.empty() ? nullptr : factory.create_mesh(mesh);
                    chunk.built = true;
                }
                if (chunk.mesh != nullptr) {
                    draws.push_back(TilemapChunkDraw{.layer_index = static_cast<int>(layer), .mesh = chunk.mesh});
                }
            }
        }
    }
    return draws;
}

}
