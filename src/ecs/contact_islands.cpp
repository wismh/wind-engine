#include "contact_islands.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <utility>

namespace engine {
namespace {

struct Island {
    ecs::Entity lowest{};
    bool has_entity{false};
    std::vector<int> pairs{};
};

}

std::vector<std::vector<int>> group_contact_islands(std::span<const ecs::Entity> entities,
                                                    std::span<const IslandBodyPair> links,
                                                    std::span<const std::uint8_t> overlapping) {
    if (links.empty() || overlapping.size() != links.size()) {
        return {};
    }
    const int body_count = static_cast<int>(entities.size());
    const int link_count = static_cast<int>(links.size());
    std::vector<int> parent(static_cast<std::size_t>(body_count));
    std::iota(parent.begin(), parent.end(), 0);
    std::vector<int> rank(static_cast<std::size_t>(body_count), 0);
    std::vector<char> used(static_cast<std::size_t>(body_count), 0);

    auto find = [&](int x) {
        int root = x;
        while (parent[static_cast<std::size_t>(root)] != root) {
            root = parent[static_cast<std::size_t>(root)];
        }
        while (parent[static_cast<std::size_t>(x)] != root) {
            const int next = parent[static_cast<std::size_t>(x)];
            parent[static_cast<std::size_t>(x)] = root;
            x = next;
        }
        return root;
    };
    auto unite = [&](int a, int b) {
        a = find(a);
        b = find(b);
        if (a == b) {
            return;
        }
        if (rank[static_cast<std::size_t>(a)] < rank[static_cast<std::size_t>(b)] ||
            (rank[static_cast<std::size_t>(a)] == rank[static_cast<std::size_t>(b)] && b < a)) {
            std::swap(a, b);
        }
        parent[static_cast<std::size_t>(b)] = a;
        if (rank[static_cast<std::size_t>(a)] == rank[static_cast<std::size_t>(b)]) {
            ++rank[static_cast<std::size_t>(a)];
        }
    };

    for (int i = 0; i < link_count; ++i) {
        if (overlapping[static_cast<std::size_t>(i)] == 0) {
            continue;
        }
        const int a = links[static_cast<std::size_t>(i)].a;
        const int b = links[static_cast<std::size_t>(i)].b;
        if (a < 0 || b < 0 || a >= body_count || b >= body_count || a == b) {
            continue;
        }
        used[static_cast<std::size_t>(a)] = 1;
        used[static_cast<std::size_t>(b)] = 1;
        unite(a, b);
    }

    std::unordered_map<int, int> island_of;
    std::vector<Island> islands;
    for (int i = 0; i < link_count; ++i) {
        const int a = links[static_cast<std::size_t>(i)].a;
        const int b = links[static_cast<std::size_t>(i)].b;
        if (a < 0 || b < 0 || a >= body_count || b >= body_count || used[static_cast<std::size_t>(a)] == 0 ||
            used[static_cast<std::size_t>(b)] == 0 || find(a) != find(b)) {
            continue;
        }
        const int root = find(a);
        const auto found = island_of.find(root);
        int slot = 0;
        if (found == island_of.end()) {
            slot = static_cast<int>(islands.size());
            island_of.emplace(root, slot);
            islands.push_back(Island{});
        } else {
            slot = found->second;
        }
        Island& island = islands[static_cast<std::size_t>(slot)];
        island.pairs.push_back(i);
        const ecs::Entity low = entities[static_cast<std::size_t>(a)] < entities[static_cast<std::size_t>(b)]
                                        ? entities[static_cast<std::size_t>(a)]
                                        : entities[static_cast<std::size_t>(b)];
        if (!island.has_entity || low < island.lowest) {
            island.lowest = low;
            island.has_entity = true;
        }
    }

    std::vector<int> order(islands.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
        if (islands[static_cast<std::size_t>(lhs)].lowest != islands[static_cast<std::size_t>(rhs)].lowest) {
            return islands[static_cast<std::size_t>(lhs)].lowest < islands[static_cast<std::size_t>(rhs)].lowest;
        }
        return lhs < rhs;
    });

    std::vector<std::vector<int>> result;
    result.reserve(order.size());
    for (int index : order) {
        result.push_back(std::move(islands[static_cast<std::size_t>(index)].pairs));
    }
    return result;
}

}
