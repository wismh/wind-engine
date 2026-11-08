#pragma once

#include <engine/nav/a_star.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::nav {

using ZoneId = int;
using PortalId = int;

inline constexpr ZoneId kInvalidZone = -1;
inline constexpr PortalId kInvalidPortal = -1;

struct Portal {
    PortalId id = kInvalidPortal;
    ZoneId zone_a = kInvalidZone;
    ZoneId zone_b = kInvalidZone;
    glm::vec3 position_a{0.0f};
    glm::vec3 position_b{0.0f};
    float traversal_cost = 1.0f;

    [[nodiscard]] ZoneId other_zone(ZoneId from) const noexcept {
        return (from == zone_a) ? zone_b : zone_a;
    }

    [[nodiscard]] glm::vec3 entry_point(ZoneId from) const noexcept {
        return (from == zone_a) ? position_a : position_b;
    }

    [[nodiscard]] glm::vec3 exit_point(ZoneId from) const noexcept {
        return (from == zone_a) ? position_b : position_a;
    }
};

// Hierarchical portal and zone graph (HPA*) for sector-to-sector and room-to-room navigation.
class PortalGraph {
public:
    PortalGraph() = default;

    void add_zone(ZoneId zone);

    PortalId add_portal(
            ZoneId zone_a,
            ZoneId zone_b,
            glm::vec3 pos_a,
            glm::vec3 pos_b,
            float traversal_cost = 1.0f);

    [[nodiscard]] bool has_zone(ZoneId zone) const noexcept;

    [[nodiscard]] const Portal* get_portal(PortalId id) const noexcept;

    // Returns the sequence of portal IDs to cross to reach goal_zone from start_zone.
    [[nodiscard]] std::vector<PortalId> find_portal_route(ZoneId start_zone, ZoneId goal_zone) const;

    // Returns high-level waypoints from start_pos to goal_pos across portal transitions.
    [[nodiscard]] std::vector<glm::vec3> find_waypoints(
            ZoneId start_zone,
            glm::vec3 start_pos,
            ZoneId goal_zone,
            glm::vec3 goal_pos) const;

private:
    struct ZoneRecord {
        std::vector<PortalId> portals;
        glm::vec3 center{0.0f};
    };

    std::unordered_map<ZoneId, ZoneRecord> zones_;
    std::vector<Portal> portals_;
};

} // namespace engine::nav
