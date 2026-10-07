#include <engine/render/rule_tile.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>

namespace engine::render {
namespace {

struct Offset {
    int x = 0;
    int y = 0;
};

// Clockwise offsets: N, NE, E, SE, S, SW, W, NW.
// Tilemap Y index grows down (South), X index grows right (East).
constexpr std::array<Offset, 8> kNeighborOffsets{{
        {0, -1},  // North
        {1, -1},  // NorthEast
        {1, 0},   // East
        {1, 1},   // SouthEast
        {0, 1},   // South
        {-1, 1},  // SouthWest
        {-1, 0},  // West
        {-1, -1}, // NorthWest
}};

std::string to_lower(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (const char c : str) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

std::optional<RuleNeighborCondition> parse_condition(std::string_view text) {
    const std::string s = to_lower(text);
    if (s == "any" || s == "ignore" || s == "*") {
        return RuleNeighborCondition::Ignore;
    }
    if (s == "this" || s == "require" || s == "x" || s == "1") {
        return RuleNeighborCondition::Require;
    }
    if (s == "not_this" || s == "forbid" || s == "!" || s == "0" || s == "empty") {
        return RuleNeighborCondition::Forbid;
    }
    return std::nullopt;
}

} // namespace

RuleTileAsset RuleTileAsset::from_desc(RuleTileDesc desc) {
    RuleTileAsset asset;
    asset.desc = std::move(desc);
    if (asset.desc.default_tile > 0) {
        asset.member_tiles.push_back(asset.desc.default_tile);
    }
    for (const auto& rule : asset.desc.rules) {
        if (rule.tile > 0 && !asset.contains_tile(rule.tile)) {
            asset.member_tiles.push_back(rule.tile);
        }
    }
    return asset;
}

bool RuleTileAsset::contains_tile(std::uint16_t tile) const {
    if (tile == 0) {
        return false;
    }
    return std::find(member_tiles.begin(), member_tiles.end(), tile) != member_tiles.end();
}

std::optional<RuleTileDesc> parse_rule_tile(std::string_view toml_text) {
    try {
        const toml::table table = toml::parse(toml_text);
        RuleTileDesc desc;

        const auto default_tile_val = table["default_tile"].value<std::int64_t>();
        if (!default_tile_val || *default_tile_val <= 0 || *default_tile_val > 0xFFFF) {
            return std::nullopt;
        }
        desc.default_tile = static_cast<std::uint16_t>(*default_tile_val);

        if (const toml::node* rules_node = table.get("rules")) {
            const toml::array* const rules_array = rules_node->as_array();
            if (rules_array == nullptr) {
                return std::nullopt;
            }
            for (const toml::node& rule_node : *rules_array) {
                const toml::table* const rule_table = rule_node.as_table();
                if (rule_table == nullptr) {
                    return std::nullopt;
                }
                RuleTileRule rule;
                if (const auto name = (*rule_table)["name"].value<std::string>()) {
                    rule.name = *name;
                }
                const auto tile_val = (*rule_table)["tile"].value<std::int64_t>();
                if (!tile_val || *tile_val <= 0 || *tile_val > 0xFFFF) {
                    return std::nullopt;
                }
                rule.tile = static_cast<std::uint16_t>(*tile_val);

                const toml::node* neighbors_node = rule_table->get("neighbors");
                const toml::array* const neighbors_array =
                        neighbors_node == nullptr ? nullptr : neighbors_node->as_array();
                if (neighbors_array == nullptr || neighbors_array->size() != 8) {
                    return std::nullopt;
                }
                for (std::size_t i = 0; i < 8; ++i) {
                    const auto cond_str = neighbors_array->get(i)->value<std::string>();
                    if (!cond_str) {
                        return std::nullopt;
                    }
                    const auto cond = parse_condition(*cond_str);
                    if (!cond) {
                        return std::nullopt;
                    }
                    rule.neighbors[i] = *cond;
                }
                desc.rules.push_back(std::move(rule));
            }
        }

        return desc;
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }
}

std::uint16_t evaluate_rule_tile(
        const Tilemap& map, int layer, int x, int y, const RuleTileAsset& rule) {
    for (const auto& candidate : rule.desc.rules) {
        bool matches = true;
        for (std::size_t i = 0; i < 8; ++i) {
            const int nx = x + kNeighborOffsets[i].x;
            const int ny = y + kNeighborOffsets[i].y;
            const std::uint16_t neighbor_tile = map.tile(layer, nx, ny);
            const bool is_this = rule.contains_tile(neighbor_tile);

            const RuleNeighborCondition cond = candidate.neighbors[i];
            if (cond == RuleNeighborCondition::Require && !is_this) {
                matches = false;
                break;
            }
            if (cond == RuleNeighborCondition::Forbid && is_this) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return candidate.tile;
        }
    }
    return rule.desc.default_tile;
}

bool set_rule_tile(
        Tilemap& map, int layer, int x, int y, const RuleTileAsset& rule, bool update_neighbors) {
    const std::uint16_t target_tile = evaluate_rule_tile(map, layer, x, y, rule);
    bool changed = map.set_tile(layer, x, y, target_tile);

    if (update_neighbors) {
        for (const auto& offset : kNeighborOffsets) {
            const int nx = x + offset.x;
            const int ny = y + offset.y;
            const std::uint16_t current = map.tile(layer, nx, ny);
            if (rule.contains_tile(current)) {
                const std::uint16_t updated = evaluate_rule_tile(map, layer, nx, ny, rule);
                if (map.set_tile(layer, nx, ny, updated)) {
                    changed = true;
                }
            }
        }
    }
    return changed;
}

bool clear_rule_tile(
        Tilemap& map, int layer, int x, int y, const RuleTileAsset& rule, bool update_neighbors) {
    bool changed = map.set_tile(layer, x, y, 0);

    if (update_neighbors) {
        for (const auto& offset : kNeighborOffsets) {
            const int nx = x + offset.x;
            const int ny = y + offset.y;
            const std::uint16_t current = map.tile(layer, nx, ny);
            if (rule.contains_tile(current)) {
                const std::uint16_t updated = evaluate_rule_tile(map, layer, nx, ny, rule);
                if (map.set_tile(layer, nx, ny, updated)) {
                    changed = true;
                }
            }
        }
    }
    return changed;
}

} // namespace engine::render
