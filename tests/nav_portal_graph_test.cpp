#include <gtest/gtest.h>

#include <engine/nav/portal_graph.h>

namespace {

using namespace engine::nav;

TEST(NavPortalGraph, SameZone) {
    PortalGraph graph;
    graph.add_zone(1);

    const glm::vec3 start{1.0f, 0.0f, 1.0f};
    const glm::vec3 goal{5.0f, 0.0f, 5.0f};

    const auto waypoints = graph.find_waypoints(1, start, 1, goal);
    ASSERT_EQ(waypoints.size(), 1u);
    EXPECT_EQ(waypoints[0], goal);
}

TEST(NavPortalGraph, MultiZoneChain) {
    PortalGraph graph;
    // Zone 1 -(P0)-> Zone 2 -(P1)-> Zone 3
    const PortalId p0 = graph.add_portal(1, 2, {10.0f, 0.0f, 0.0f}, {11.0f, 0.0f, 0.0f}, 1.0f);
    const PortalId p1 = graph.add_portal(2, 3, {20.0f, 0.0f, 0.0f}, {21.0f, 0.0f, 0.0f}, 1.0f);

    const auto route = graph.find_portal_route(1, 3);
    ASSERT_EQ(route.size(), 2u);
    EXPECT_EQ(route[0], p0);
    EXPECT_EQ(route[1], p1);

    const glm::vec3 start{0.0f, 0.0f, 0.0f};
    const glm::vec3 goal{30.0f, 0.0f, 0.0f};
    const auto waypoints = graph.find_waypoints(1, start, 3, goal);
    EXPECT_FALSE(waypoints.empty());
    EXPECT_EQ(waypoints.back(), goal);
}

TEST(NavPortalGraph, DisconnectedZones) {
    PortalGraph graph;
    graph.add_zone(1);
    graph.add_zone(2);

    const auto route = graph.find_portal_route(1, 2);
    EXPECT_TRUE(route.empty());

    const auto waypoints = graph.find_waypoints(1, {0.0f, 0.0f, 0.0f}, 2, {10.0f, 0.0f, 0.0f});
    EXPECT_TRUE(waypoints.empty());
}

} // namespace
