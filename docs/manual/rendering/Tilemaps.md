# Tilemaps

Wind provides a high-performance, chunk-based tilemap rendering system in `engine::render::Tilemap` (`include/engine/render/tilemap.h`). Tilemaps render grids of tiles cut from a single tileset texture, support multiple layers, frustum culling per chunk, and dynamic in-memory cell modification with dirty-chunk rebuilding.

---

## 1. The `.tilemap` Asset

Tilemap definitions are authored as TOML documents with the `.tilemap` extension (`importer = "tilemap"`):

```toml
# assets/maps/overworld.tilemap
texture = "0123456789abcdef0123456789abcdef"   # GUID of the tileset texture asset (.png)
size = [64, 64]                                # Map width and height in cells
tile_size = [16, 16]                           # Size of one tile in texture pixels
columns = 8                                    # Optional: tiles per row in texture (0 auto-resolves)
pixels_per_unit = 16.0                         # World units = tile_size / pixels_per_unit (here: 1 unit = 1 tile)

[[layers]]
name = "ground"
tiles = [
    1, 1, 1, 1, 2, 2, 2, 2,
    # size.x * size.y integers (row-major, row 0 at the top)
    # 0 = empty cell, n > 0 = tile index (n - 1) in the tileset (left-to-right, top-to-bottom)
]

[[layers]]
name = "decorations"
tiles = [
    0, 0, 5, 0, 0, 0, 0, 0,
]
```

### Tilemap File Specifications:
- **`texture`**: 32-hex GUID string of the tileset PNG asset. For pixel art, specify `filter = "nearest"` in the PNG's `.meta` file to prevent bleeding.
- **`size`**: Array of two integers `[width, height]`.
- **`tile_size`**: Pixel dimensions of each tile quad `[tile_width, tile_height]`.
- **`columns`**: Number of tile columns in the texture. If omitted or `0`, it is calculated automatically as `texture_width / tile_size.x`.
- **`pixels_per_unit`**: Controls conversion from pixel dimensions to ECS world coordinates (`cell_size = tile_size / pixels_per_unit`). Default is `100.0f`.
- **`tiles`**: Flat array of `std::uint16_t` values of length `size.x * size.y`.
  - `0`: Empty cell (no quad is baked).
  - `n > 0`: Tile index `n - 1` from the tileset, ordered left-to-right, top-to-bottom.
- Tilemap assets are validated during asset cooking (`asset_codegen`). Non-positive sizes, invalid GUIDs, or layer tile counts differing from `width * height` fail the build with `CodegenErrorKind::Tilemap`.

---

## 2. Spawning a Tilemap in ECS

Load the tilemap asset via `AssetsDb::get<engine::render::TilemapAsset>()` and attach `engine::render::Tilemap` alongside `engine::Transform`:

```cpp
#include <engine/ecs/transform.h>
#include <engine/render/tilemap.h>
#include <engine/resources/assets_db.h>
#include <asset_ids.h>

void spawn_level(engine::ecs::World& world, engine::AssetsDb& assets) {
    auto map_asset = assets.get<engine::render::TilemapAsset>(assets::maps::overworld);

    engine::ecs::Entity map_entity = world.create();
    world.emplace<engine::Transform>(map_entity, engine::Transform{
        .position = {0.0f, 0.0f, 0.0f}
    });

    engine::render::Tilemap tilemap = engine::render::Tilemap::from_asset(map_asset);
    tilemap.layer = 0;              // Render layer
    tilemap.order_in_layer = 0;     // Base sorting order
    tilemap.color = {1.0f, 1.0f, 1.0f, 1.0f}; // Tint multiplier

    world.emplace<engine::render::Tilemap>(map_entity, std::move(tilemap));
}
```

### Orientation & Coordinate System
- Cell `(0, 0)` is placed at the entity's `Transform::position` (top-left).
- **X grows right** (`x * cell_width`).
- **Y grows down in cell indices**, which corresponds to **decreasing Y in world coordinates** (row `r` spans `y` in `[-(r + 1) * cell_height, -r * cell_height]`). The map extends downwards and to the right from the origin.
- Layer `i` draws at sorting order `order_in_layer + i`, so higher layer indices automatically render on top of lower ones.

---

## 3. Chunking & Culling Performance

Wind slices each tilemap layer into **32 × 32 cell chunks** (`kTilemapChunkSize = 32`):
- Each chunk with at least one non-empty tile is baked into a single static GPU mesh (`IMesh`).
- Empty cells produce no vertices.
- Chunks outside the camera's view frustum for all bound windows are automatically culled (`visible_tilemap_chunks` with one `engine::render::Frustum` per window, `<engine/render/frustum.h>`), and a chunk is baked only once it is first seen. Sprites and 3D meshes are culled the same way.
- Chunks are drawn using `CmdDrawMesh` with the shared per-texture unlit material from `SpriteMaterialCache`, minimizing shader switches and state changes.

---

## 4. Dynamic Runtime Modifications (`set_tile`)

You can query and modify tiles dynamically at runtime (e.g. for destructible terrain, mining, or placing buildings):

```cpp
auto& tilemap = world.get<engine::render::Tilemap>(map_entity);

// 1. Read cell at layer 0, (x=10, y=5)
std::uint16_t current_tile = tilemap.tile(0, 10, 5);

// 2. Change cell to tile index 4 (1-based: 0=empty, 5=tile #4)
bool modified = tilemap.set_tile(0, 10, 5, 5);

// 3. Clear cell (0 = empty)
tilemap.set_tile(0, 10, 5, 0);
```

### Behavior of `set_tile`:
- Returns `false` and makes no changes if the coordinates are out of map bounds, the layer index is invalid, or the cell already has that tile value.
- When modified, **only the single 32 × 32 chunk containing that cell is marked dirty and rebuilt**. Neighboring chunks are untouched.
- Modifications affect the runtime `Tilemap` component only; the underlying `TilemapAsset` remains immutable.

---

## 5. Animated Tiles (GPU Shader-Driven)

Wind supports GPU-accelerated animated tiles without CPU mesh re-baking every frame. Tile animation metadata is specified in the `.tilemap` TOML document:

```toml
# assets/maps/overworld.tilemap
texture = "0123456789abcdef0123456789abcdef"
size = [64, 64]
tile_size = [16, 16]

[[animations]]
tile = 5         # 1-based base tile index in the layers
frames = 4       # Number of consecutive horizontal frames in the tileset
fps = 8.0        # Playback speed (frames per second)
```

### How It Works:
- During chunk baking (`bake_tilemap_chunk`), quads for animated tiles encode `[frames, fps, uv_step_u]` into the vertex animation parameters (`MeshVertex::normal`).
- The unlit vertex shader calculates `uv.x += floor(mod(uTime * fps, frames)) * uv_step_u` on the GPU.
- Static tiles incur zero shader overhead (`aAnim.x <= 1.0`).
- No CPU vertex buffer allocations or chunk rebuilds happen during playback, providing maximum performance for large water bodies, lava, or waterfalls.

---

## 6. RuleTiles (Autotiling)

Wind provides a declarative RuleTile system (`<engine/render/rule_tile.h>`) for automatic neighbor-aware tile selection (Blob / 8-neighbor / 3×3 autotiling).

### The `.ruletile` Definition (TOML)

```toml
# assets/tiles/grass.ruletile
default_tile = 1

# 8 neighbors clockwise starting from North: [N, NE, E, SE, S, SW, W, NW]
# Condition strings:
#  - "this", "require", "x", "1": neighbor must be of this rule tile family
#  - "not_this", "forbid", "!", "0": neighbor must be different or empty
#  - "any", "ignore", "*": condition ignored

[[rules]]
name = "full_surrounded"
tile = 5
neighbors = ["this", "this", "this", "this", "this", "this", "this", "this"]

[[rules]]
name = "top_edge"
tile = 2
neighbors = ["not_this", "any", "this", "any", "this", "any", "this", "any"]
```

### Placing and Clearing RuleTiles

```cpp
#include <engine/render/rule_tile.h>

// Parse rule tile asset or load from TOML
auto desc = engine::render::parse_rule_tile(ruletile_toml);
engine::render::RuleTileAsset grass_rule = engine::render::RuleTileAsset::from_desc(std::move(*desc));

// Place rule tile at (x=10, y=5) and automatically propagate to all 8 neighbors:
engine::render::set_rule_tile(tilemap, /*layer=*/0, /*x=*/10, /*y=*/5, grass_rule, /*update_neighbors=*/true);

// Clear rule tile and automatically update neighbor border edges:
engine::render::clear_rule_tile(tilemap, /*layer=*/0, /*x=*/10, /*y=*/5, grass_rule, /*update_neighbors=*/true);
```

---

## 7. World Serialization

Tilemaps are fully integrated with Wind's `.world` scene serializer:
- The `tilemap` serializer entry persists the asset GUID, `color`, `layer`, and `order_in_layer`.
- Map cells are reloaded from the asset on scene load.

---

## Next Steps

- Add dynamic particle effects with [Particle Systems](Particles.md).
- Control the viewport and coordinate transforms with [Camera](Camera.md).
- Manage sorting order between sprites, tilemaps, and meshes with [Command Buffer & Sorting](Command-Buffer-and-Sort.md).
