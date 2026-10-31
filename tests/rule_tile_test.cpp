#include <gtest/gtest.h>

#include <engine/render/rule_tile.h>
#include <engine/render/tilemap.h>

#include <memory>
#include <string>

namespace {

using engine::render::RuleNeighborCondition;
using engine::render::RuleTileAsset;
using engine::render::RuleTileDesc;
using engine::render::RuleTileRule;
using engine::render::Tilemap;
using engine::render::TilemapAsset;

std::shared_ptr<TilemapAsset> make_test_map_asset(int width = 10, int height = 10) {
    auto asset = std::make_shared<TilemapAsset>();
    asset->desc.width = width;
    asset->desc.height = height;
    asset->desc.tile_size = {16, 16};
    asset->desc.columns = 4;
    asset->desc.pixels_per_unit = 16.0f;
    asset->desc.layers.push_back({"ground", std::vector<std::uint16_t>(static_cast<std::size_t>(width * height), 0)});
    asset->texture_size = {64, 64};
    return asset;
}

} // namespace

TEST(RuleTile, ParsesValidRuleTileToml) {
    const std::string toml = R"(
default_tile = 1

[[rules]]
name = "surrounded"
tile = 5
neighbors = ["this", "this", "this", "this", "this", "this", "this", "this"]

[[rules]]
name = "top_edge"
tile = 2
neighbors = ["not_this", "any", "this", "any", "this", "any", "this", "any"]
)";

    const auto desc = engine::render::parse_rule_tile(toml);
    ASSERT_TRUE(desc.has_value());
    EXPECT_EQ(desc->default_tile, 1);
    ASSERT_EQ(desc->rules.size(), 2u);

    EXPECT_EQ(desc->rules[0].name, "surrounded");
    EXPECT_EQ(desc->rules[0].tile, 5);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(desc->rules[0].neighbors[static_cast<std::size_t>(i)], RuleNeighborCondition::Require);
    }

    EXPECT_EQ(desc->rules[1].name, "top_edge");
    EXPECT_EQ(desc->rules[1].tile, 2);
    EXPECT_EQ(desc->rules[1].neighbors[0], RuleNeighborCondition::Forbid);
    EXPECT_EQ(desc->rules[1].neighbors[1], RuleNeighborCondition::Ignore);
    EXPECT_EQ(desc->rules[1].neighbors[2], RuleNeighborCondition::Require);
}

TEST(RuleTile, ParseRejectsInvalidToml) {
    // Missing default_tile
    EXPECT_FALSE(engine::render::parse_rule_tile("").has_value());
    // Negative or zero default_tile
    EXPECT_FALSE(engine::render::parse_rule_tile("default_tile = 0").has_value());
    EXPECT_FALSE(engine::render::parse_rule_tile("default_tile = -1").has_value());

    // Neighbor count not 8
    const std::string bad_neighbors = R"(
default_tile = 1
[[rules]]
tile = 2
neighbors = ["this", "this"]
)";
    EXPECT_FALSE(engine::render::parse_rule_tile(bad_neighbors).has_value());

    // Invalid neighbor condition
    const std::string unknown_cond = R"(
default_tile = 1
[[rules]]
tile = 2
neighbors = ["this", "this", "this", "this", "invalid_condition", "this", "this", "this"]
)";
    EXPECT_FALSE(engine::render::parse_rule_tile(unknown_cond).has_value());
}

TEST(RuleTile, AssetContainsTiles) {
    RuleTileDesc desc;
    desc.default_tile = 1;
    desc.rules.push_back(RuleTileRule{.name = "edge", .tile = 2});
    desc.rules.push_back(RuleTileRule{.name = "center", .tile = 5});

    const RuleTileAsset asset = RuleTileAsset::from_desc(std::move(desc));
    EXPECT_FALSE(asset.contains_tile(0));
    EXPECT_TRUE(asset.contains_tile(1));
    EXPECT_TRUE(asset.contains_tile(2));
    EXPECT_TRUE(asset.contains_tile(5));
    EXPECT_FALSE(asset.contains_tile(3));
    EXPECT_FALSE(asset.contains_tile(99));
}

TEST(RuleTile, EvaluateMatchesRuleOrDefaults) {
    RuleTileDesc desc;
    desc.default_tile = 1;

    // Rule: if North and South are "this", return tile 8
    RuleTileRule rule;
    rule.tile = 8;
    rule.neighbors[0] = RuleNeighborCondition::Require; // North
    rule.neighbors[4] = RuleNeighborCondition::Require; // South
    desc.rules.push_back(rule);

    const RuleTileAsset asset = RuleTileAsset::from_desc(std::move(desc));
    Tilemap map = Tilemap::from_asset(make_test_map_asset(5, 5));

    // Initially all cells are 0: should return default_tile (1)
    EXPECT_EQ(engine::render::evaluate_rule_tile(map, 0, 2, 2, asset), 1);

    // Place tile 1 at North (2, 1) and South (2, 3)
    map.set_tile(0, 2, 1, 1);
    map.set_tile(0, 2, 3, 1);

    // Now condition holds: should evaluate to 8
    EXPECT_EQ(engine::render::evaluate_rule_tile(map, 0, 2, 2, asset), 8);
}

TEST(RuleTile, SetRuleTileUpdatesCellAndPropagatesToNeighbors) {
    // Define a 3-tile setup:
    // tile 1: isolated (all 4 cardinal neighbors forbid)
    // tile 2: vertical connection (North and South require)
    // tile 3: full center (all 8 require)
    RuleTileDesc desc;
    desc.default_tile = 1;

    RuleTileRule center_rule;
    center_rule.tile = 3;
    center_rule.neighbors.fill(RuleNeighborCondition::Require);
    desc.rules.push_back(center_rule);

    RuleTileRule vertical_rule;
    vertical_rule.tile = 2;
    vertical_rule.neighbors[0] = RuleNeighborCondition::Require; // North
    vertical_rule.neighbors[4] = RuleNeighborCondition::Require; // South
    desc.rules.push_back(vertical_rule);

    const RuleTileAsset asset = RuleTileAsset::from_desc(std::move(desc));
    Tilemap map = Tilemap::from_asset(make_test_map_asset(5, 5));

    // Place at (2, 2)
    EXPECT_TRUE(engine::render::set_rule_tile(map, 0, 2, 2, asset));
    EXPECT_EQ(map.tile(0, 2, 2), 1); // isolated -> default 1

    // Place at (2, 1) - above (2, 2)
    EXPECT_TRUE(engine::render::set_rule_tile(map, 0, 2, 1, asset));

    // Place at (2, 3) - below (2, 2)
    // When (2, 3) is placed, neighbor (2, 2) has both North (2, 1) and South (2, 3), so (2, 2) should become 2!
    EXPECT_TRUE(engine::render::set_rule_tile(map, 0, 2, 3, asset));
    EXPECT_EQ(map.tile(0, 2, 2), 2);

    // Clear tile at (2, 3): (2, 2) loses its South neighbor and reverts back to 1!
    EXPECT_TRUE(engine::render::clear_rule_tile(map, 0, 2, 3, asset));
    EXPECT_EQ(map.tile(0, 2, 3), 0);
    EXPECT_EQ(map.tile(0, 2, 2), 1);
}
