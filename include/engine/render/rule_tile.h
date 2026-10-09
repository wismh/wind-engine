#pragma once

// docs/manual/rendering/Tilemaps.md

#include <engine/render/tilemap.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

enum class RuleNeighborCondition : std::uint8_t {
    Ignore,
    Require,
    Forbid,
};

// 8 neighbor directions clockwise starting from North (0: North, 1: NorthEast, 2: East, 3: SouthEast,
// 4: South, 5: SouthWest, 6: West, 7: NorthWest).
enum class RuleNeighborDirection : std::size_t {
    North = 0,
    NorthEast = 1,
    East = 2,
    SouthEast = 3,
    South = 4,
    SouthWest = 5,
    West = 6,
    NorthWest = 7,
};

struct RuleTileRule {
    std::string name;
    std::array<RuleNeighborCondition, 8> neighbors{RuleNeighborCondition::Ignore};
    std::uint16_t tile = 0;
};

struct RuleTileDesc {
    std::uint16_t default_tile = 0;
    std::vector<RuleTileRule> rules;
};

struct RuleTileAsset {
    RuleTileDesc desc;
    std::vector<std::uint16_t> member_tiles;

    [[nodiscard]] static RuleTileAsset from_desc(RuleTileDesc desc);
    [[nodiscard]] bool contains_tile(std::uint16_t tile) const;
};

// Parses a `.ruletile` TOML string. Returns nullopt on parse errors, invalid/negative tile indices, or
// neighbor arrays that are not exactly 8 elements.
[[nodiscard]] std::optional<RuleTileDesc> parse_rule_tile(std::string_view toml_text);

// Evaluates the matching tile index for cell (x, y) on the given layer of the map against the rule set.
[[nodiscard]] std::uint16_t evaluate_rule_tile(
        const Tilemap& map, int layer, int x, int y, const RuleTileAsset& rule);

// Sets cell (x, y) to the matching rule tile. When `update_neighbors` is true, re-evaluates all adjacent
// cells that belong to this rule tile family so boundaries adjust automatically. Returns true if any tile changed.
bool set_rule_tile(
        Tilemap& map, int layer, int x, int y, const RuleTileAsset& rule, bool update_neighbors = true);

// Clears cell (x, y) to 0 (empty). When `update_neighbors` is true, re-evaluates all adjacent cells that belong
// to this rule tile family. Returns true if any tile changed.
bool clear_rule_tile(
        Tilemap& map, int layer, int x, int y, const RuleTileAsset& rule, bool update_neighbors = true);

} // namespace engine::render
