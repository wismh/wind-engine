#include <engine/nav/portal_graph.h>

#include <algorithm>
#include <queue>
#include <unordered_map>

namespace engine::nav {

void PortalGraph::add_zone(ZoneId zone) {
    zones_.try_emplace(zone, ZoneRecord{});
}

PortalId PortalGraph::add_portal(
        ZoneId zone_a,
        ZoneId zone_b,
        glm::vec3 pos_a,
        glm::vec3 pos_b,
        float traversal_cost) {
    add_zone(zone_a);
    add_zone(zone_b);

    const PortalId id = static_cast<PortalId>(portals_.size());
    portals_.push_back(Portal{
            .id = id,
            .zone_a = zone_a,
            .zone_b = zone_b,
            .position_a = pos_a,
            .position_b = pos_b,
            .traversal_cost = traversal_cost,
    });

    zones_[zone_a].portals.push_back(id);
    zones_[zone_b].portals.push_back(id);
    return id;
}

bool PortalGraph::has_zone(ZoneId zone) const noexcept {
    return zones_.contains(zone);
}

const Portal* PortalGraph::get_portal(PortalId id) const noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= portals_.size()) {
        return nullptr;
    }
    return &portals_[static_cast<std::size_t>(id)];
}

std::vector<PortalId> PortalGraph::find_portal_route(ZoneId start_zone, ZoneId goal_zone) const {
    if (start_zone == goal_zone || !has_zone(start_zone) || !has_zone(goal_zone)) {
        return {};
    }

    struct Step {
        ZoneId from_zone = kInvalidZone;
        PortalId via_portal = kInvalidPortal;
        float cost = 1e9f;
    };

    struct QueueEntry {
        float cost = 0.0f;
        ZoneId zone = kInvalidZone;

        bool operator>(const QueueEntry& other) const noexcept {
            return cost > other.cost;
        }
    };

    std::unordered_map<ZoneId, Step> visited;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;

    visited[start_zone] = Step{.from_zone = kInvalidZone, .via_portal = kInvalidPortal, .cost = 0.0f};
    queue.push(QueueEntry{.cost = 0.0f, .zone = start_zone});

    bool found = false;

    while (!queue.empty()) {
        const QueueEntry top = queue.top();
        queue.pop();

        if (top.zone == goal_zone) {
            found = true;
            break;
        }

        if (top.cost > visited[top.zone].cost) {
            continue;
        }

        const auto it = zones_.find(top.zone);
        if (it == zones_.end()) {
            continue;
        }

        for (PortalId p_id : it->second.portals) {
            const Portal& portal = portals_[static_cast<std::size_t>(p_id)];
            const ZoneId next_zone = portal.other_zone(top.zone);
            const float new_cost = top.cost + portal.traversal_cost;

            auto [entry, inserted] = visited.try_emplace(next_zone, Step{
                    .from_zone = top.zone,
                    .via_portal = p_id,
                    .cost = new_cost,
            });

            if (!inserted && new_cost < entry->second.cost) {
                entry->second.from_zone = top.zone;
                entry->second.via_portal = p_id;
                entry->second.cost = new_cost;
                queue.push(QueueEntry{.cost = new_cost, .zone = next_zone});
            } else if (inserted) {
                queue.push(QueueEntry{.cost = new_cost, .zone = next_zone});
            }
        }
    }

    if (!found) {
        return {};
    }

    std::vector<PortalId> route;
    ZoneId curr = goal_zone;
    while (curr != start_zone) {
        const Step& step = visited.at(curr);
        route.push_back(step.via_portal);
        curr = step.from_zone;
    }

    std::reverse(route.begin(), route.end());
    return route;
}

std::vector<glm::vec3> PortalGraph::find_waypoints(
        ZoneId start_zone,
        [[maybe_unused]] glm::vec3 start_pos,
        ZoneId goal_zone,
        glm::vec3 goal_pos) const {
    if (start_zone == goal_zone) {
        return {goal_pos};
    }

    const std::vector<PortalId> route = find_portal_route(start_zone, goal_zone);
    if (route.empty()) {
        return {};
    }

    std::vector<glm::vec3> waypoints;
    waypoints.reserve(route.size() * 2 + 1);

    ZoneId curr_zone = start_zone;
    for (PortalId pid : route) {
        const Portal& p = portals_[static_cast<std::size_t>(pid)];
        const glm::vec3 entry = p.entry_point(curr_zone);
        const glm::vec3 exit = p.exit_point(curr_zone);

        if (waypoints.empty() || glm::distance(waypoints.back(), entry) > 1e-3f) {
            waypoints.push_back(entry);
        }
        if (glm::distance(entry, exit) > 1e-3f) {
            waypoints.push_back(exit);
        }

        curr_zone = p.other_zone(curr_zone);
    }

    waypoints.push_back(goal_pos);
    return waypoints;
}

} // namespace engine::nav
