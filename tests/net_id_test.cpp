#include <gtest/gtest.h>

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/net_id.h>

#include <unordered_map>
#include <unordered_set>

namespace engine::net {

TEST(NetId, ValidityAndComparison) {
    EXPECT_EQ(kInvalidNetId, 0u);

    const NetId id1 = 1;
    const NetId id2 = 2;
    const NetId id1_dup = 1;

    EXPECT_NE(id1, kInvalidNetId);
    EXPECT_EQ(id1, id1_dup);
    EXPECT_NE(id1, id2);
    EXPECT_LT(id1, id2);
}

TEST(NetId, HashingInStandardContainers) {
    std::unordered_set<NetId> set;
    set.insert(10);
    set.insert(20);
    set.insert(10);

    EXPECT_EQ(set.size(), 2u);
    EXPECT_TRUE(set.contains(10));
    EXPECT_TRUE(set.contains(20));
    EXPECT_FALSE(set.contains(30));

    std::unordered_map<NetId, std::string> map;
    map[1] = "player1";
    map[2] = "player2";

    EXPECT_EQ(map[1], "player1");
    EXPECT_EQ(map[2], "player2");
}

TEST(NetIdGenerator, MonotonicGenerationAndReset) {
    NetIdGenerator gen;
    EXPECT_EQ(gen.peek_next_value(), 1u);

    EXPECT_EQ(gen.next(), 1u);
    EXPECT_EQ(gen.next(), 2u);
    EXPECT_EQ(gen.next(), 3u);
    EXPECT_EQ(gen.peek_next_value(), 4u);

    gen.reset(100);
    EXPECT_EQ(gen.next(), 100u);
    EXPECT_EQ(gen.next(), 101u);

    // Zero start should safely fallback to 1
    gen.reset(0);
    EXPECT_EQ(gen.next(), 1u);
}

TEST(NetEntityMap, BindLookupAndUnbind) {
    NetEntityMap map;
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0u);

    const ecs::Entity e1{1, 1};
    const ecs::Entity e2{2, 1};
    const NetId id1 = 100;
    const NetId id2 = 200;

    // Invalid NetId fails
    EXPECT_FALSE(map.bind(kInvalidNetId, e1));

    // Valid bind
    EXPECT_TRUE(map.bind(id1, e1));
    EXPECT_EQ(map.size(), 1u);
    EXPECT_FALSE(map.empty());

    // Duplicate NetId or Entity fails
    EXPECT_FALSE(map.bind(id1, e2));
    EXPECT_FALSE(map.bind(id2, e1));

    EXPECT_TRUE(map.bind(id2, e2));
    EXPECT_EQ(map.size(), 2u);

    // Queries
    EXPECT_TRUE(map.contains(id1));
    EXPECT_TRUE(map.contains(e1));
    EXPECT_EQ(map.find_entity(id1), e1);
    EXPECT_EQ(map.find_net_id(e1), id1);

    EXPECT_EQ(map.find_entity(999), std::nullopt);
    EXPECT_EQ(map.find_net_id(ecs::Entity{999, 1}), std::nullopt);

    // Unbind by NetId
    EXPECT_TRUE(map.unbind(id1));
    EXPECT_FALSE(map.contains(id1));
    EXPECT_FALSE(map.contains(e1));
    EXPECT_EQ(map.size(), 1u);

    // Unbind by Entity
    EXPECT_TRUE(map.unbind(e2));
    EXPECT_FALSE(map.contains(id2));
    EXPECT_FALSE(map.contains(e2));
    EXPECT_TRUE(map.empty());

    // Unbinding non-existent fails gracefully
    EXPECT_FALSE(map.unbind(id1));
    EXPECT_FALSE(map.unbind(e1));
}

TEST(NetEntityMap, WorldContextIntegration) {
    ecs::World world;
    const ecs::Entity e1 = world.create();
    const ecs::Entity e2 = world.create();

    const NetId id1 = 10;
    const NetId id2 = 20;

    // Binding attaches NetIdentity component and adds to world context map
    EXPECT_TRUE(bind_net_entity(world, e1, id1));
    EXPECT_TRUE(bind_net_entity(world, e2, id2));

    const auto* ident1 = world.try_get<NetIdentity>(e1);
    ASSERT_NE(ident1, nullptr);
    EXPECT_EQ(ident1->id, id1);

    EXPECT_EQ(find_entity(world, id1), e1);
    EXPECT_EQ(find_entity(world, id2), e2);
    EXPECT_EQ(find_net_id(world, e1), id1);
    EXPECT_EQ(find_net_id(world, e2), id2);

    // Unbind removes component and unregisters from map
    EXPECT_TRUE(unbind_net_entity(world, e1));
    EXPECT_EQ(world.try_get<NetIdentity>(e1), nullptr);
    EXPECT_EQ(find_entity(world, id1), std::nullopt);
    EXPECT_EQ(find_net_id(world, e1), std::nullopt);

    // e2 remains intact
    EXPECT_EQ(find_entity(world, id2), e2);
}

TEST(NetEntityMap, HierarchyParentTranslation) {
    ecs::World world;
    const ecs::Entity parent_e = world.create();
    const ecs::Entity child_e = world.create();

    const NetId parent_net = 1001;
    const NetId child_net = 1002;

    ASSERT_TRUE(bind_net_entity(world, parent_e, parent_net));
    ASSERT_TRUE(bind_net_entity(world, child_e, child_net));

    // Linking parent by NetId
    EXPECT_TRUE(set_parent_by_net_id(world, child_net, parent_net));

    const auto* parent_comp = world.try_get<Parent>(child_e);
    ASSERT_NE(parent_comp, nullptr);
    EXPECT_EQ(parent_comp->entity, parent_e);

    // Translate back: find parent's NetId from child entity
    const auto resolved_parent_net = get_parent_net_id(world, child_e);
    ASSERT_TRUE(resolved_parent_net.has_value());
    EXPECT_EQ(*resolved_parent_net, parent_net);

    // Non-existent NetId fails
    EXPECT_FALSE(set_parent_by_net_id(world, child_net, 9999));
    EXPECT_FALSE(set_parent_by_net_id(world, 9999, parent_net));
}

} // namespace engine::net
