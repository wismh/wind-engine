#pragma once

#include <engine/nav/nav_types.h>

#include <algorithm>
#include <concepts>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace engine::nav {

// Concept for any navigable graph structure (2D grid, 3D voxel, waypoint network, or portal graph).
template <typename G, typename Node>
concept NavGraph = requires(const G& graph, const Node& node,
                            std::function<void(const Node&, float)> visitor) {
    { graph.heuristic(node, node) } -> std::convertible_to<float>;
};

// Generic, budgeted A* pathfinder.
template <typename Graph, typename Node, typename Hash = NavNodeHash<Node>, typename Equal = std::equal_to<Node>>
[[nodiscard]] NavPath<Node> a_star_find_path(
        const Graph& graph,
        const Node& start,
        const Node& goal,
        const NavSearchParams& params = {}) {
    if (Equal{}(start, goal)) {
        NavPath<Node> path;
        path.status = PathStatus::Complete;
        path.total_cost = 0.0f;
        if (params.include_start) {
            path.nodes.push_back(start);
        }
        return path;
    }

    struct InternalNode {
        float cost = 0.0f;
        Node parent{};
        bool closed = false;
    };

    struct QueueEntry {
        float priority = 0.0f;
        Node node{};

        bool operator>(const QueueEntry& other) const noexcept {
            return priority > other.priority;
        }
    };

    std::unordered_map<Node, InternalNode, Hash, Equal> nodes;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> open;

    nodes.emplace(start, InternalNode{.cost = 0.0f, .parent = start, .closed = false});
    open.push(QueueEntry{.priority = graph.heuristic(start, goal), .node = start});

    Node best = start;
    float best_estimate = graph.heuristic(start, goal);
    bool reached = false;
    int visited = 0;

    while (!open.empty() && visited < params.budget) {
        const Node current = open.top().node;
        open.pop();

        auto it = nodes.find(current);
        if (it == nodes.end() || it->second.closed) {
            continue;
        }

        it->second.closed = true;
        ++visited;
        const float current_cost = it->second.cost;

        const float remaining = graph.heuristic(current, goal);
        if (Equal{}(current, goal)) {
            best = current;
            reached = true;
            break;
        }

        if (remaining < best_estimate) {
            best_estimate = remaining;
            best = current;
        }

        graph.neighbors(current, [&](const Node& neighbor, float step_cost) {
            const float next_cost = current_cost + step_cost;
            auto [found, inserted] = nodes.try_emplace(neighbor, InternalNode{next_cost, current, false});
            if (!inserted) {
                if (found->second.closed || found->second.cost <= next_cost) {
                    return;
                }
                found->second.cost = next_cost;
                found->second.parent = current;
            }
            open.push(QueueEntry{
                    .priority = next_cost + graph.heuristic(neighbor, goal),
                    .node = neighbor,
            });
        });
    }

    if (!reached && (!params.allow_partial || Equal{}(best, start))) {
        return NavPath<Node>{.status = PathStatus::NotFound};
    }

    NavPath<Node> path;
    path.status = reached ? PathStatus::Complete : PathStatus::Partial;
    path.total_cost = nodes.at(best).cost;

    for (Node curr = best; !Equal{}(curr, start); curr = nodes.at(curr).parent) {
        path.nodes.push_back(curr);
    }

    if (params.include_start) {
        path.nodes.push_back(start);
    }

    std::reverse(path.nodes.begin(), path.nodes.end());
    return path;
}

} // namespace engine::nav
