#include "contact_islands.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine {
namespace {

struct IslandBuilder {
    ecs::Entity lowest_dynamic{};
    bool has_dynamic{false};
    ecs::Entity lowest_any{};
    bool has_any{false};
    int dynamic_body_count{0};
    std::vector<int> pairs{};
};

[[nodiscard]] std::uint64_t pack_pair(int root, int static_body) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(root)) << 32u) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_body));
}

}

std::vector<ContactIsland> build_contact_islands(std::span<const ecs::Entity> entities,
                                                 std::span<const IslandBodyPair> links,
                                                 std::span<const std::uint8_t> overlapping,
                                                 std::span<const std::uint8_t> dynamic_body) {
    if (links.empty() || overlapping.size() != links.size()) {
        return {};
    }
    const int body_count = static_cast<int>(entities.size());
    const int link_count = static_cast<int>(links.size());
    const bool has_dyn_mask = (dynamic_body.size() == entities.size());

    auto is_dyn = [&](int body) noexcept {
        if (!has_dyn_mask) {
            return true;
        }
        return dynamic_body[static_cast<std::size_t>(body)] != 0;
    };

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
        const bool dyn_a = is_dyn(a);
        const bool dyn_b = is_dyn(b);
        if (!dyn_a && !dyn_b) {
            continue;
        }
        if (dyn_a) {
            used[static_cast<std::size_t>(a)] = 1;
        }
        if (dyn_b) {
            used[static_cast<std::size_t>(b)] = 1;
        }
        if (dyn_a && dyn_b) {
            unite(a, b);
        }
    }

    // Record which static/kinematic bodies overlap with which dynamic island root.
    std::unordered_set<std::uint64_t> island_static_contacts;
    if (has_dyn_mask) {
        for (int i = 0; i < link_count; ++i) {
            if (overlapping[static_cast<std::size_t>(i)] == 0) {
                continue;
            }
            const int a = links[static_cast<std::size_t>(i)].a;
            const int b = links[static_cast<std::size_t>(i)].b;
            if (a < 0 || b < 0 || a >= body_count || b >= body_count || a == b) {
                continue;
            }
            const bool dyn_a = is_dyn(a);
            const bool dyn_b = is_dyn(b);
            if (dyn_a && !dyn_b && used[static_cast<std::size_t>(a)] != 0) {
                island_static_contacts.insert(pack_pair(find(a), b));
            } else if (!dyn_a && dyn_b && used[static_cast<std::size_t>(b)] != 0) {
                island_static_contacts.insert(pack_pair(find(b), a));
            }
        }
    }

    std::unordered_map<int, int> island_of;
    std::vector<IslandBuilder> islands;
    const std::size_t max_islands = std::min(entities.size(), links.size());
    island_of.reserve(max_islands);
    islands.reserve(max_islands);

    auto get_or_create_island = [&](int root) {
        const auto found = island_of.find(root);
        if (found != island_of.end()) {
            return found->second;
        }
        const int slot = static_cast<int>(islands.size());
        island_of.emplace(root, slot);
        islands.push_back(IslandBuilder{});
        return slot;
    };

    for (int i = 0; i < link_count; ++i) {
        const int a = links[static_cast<std::size_t>(i)].a;
        const int b = links[static_cast<std::size_t>(i)].b;
        if (a < 0 || b < 0 || a >= body_count || b >= body_count || a == b) {
            continue;
        }
        const bool dyn_a = is_dyn(a);
        const bool dyn_b = is_dyn(b);
        int root = -1;
        if (dyn_a && dyn_b) {
            if (used[static_cast<std::size_t>(a)] == 0 || used[static_cast<std::size_t>(b)] == 0 ||
                find(a) != find(b)) {
                continue;
            }
            root = find(a);
        } else if (dyn_a && !dyn_b) {
            if (used[static_cast<std::size_t>(a)] == 0) {
                continue;
            }
            const int r = find(a);
            if (overlapping[static_cast<std::size_t>(i)] == 0 &&
                !island_static_contacts.contains(pack_pair(r, b))) {
                continue;
            }
            root = r;
        } else if (!dyn_a && dyn_b) {
            if (used[static_cast<std::size_t>(b)] == 0) {
                continue;
            }
            const int r = find(b);
            if (overlapping[static_cast<std::size_t>(i)] == 0 &&
                !island_static_contacts.contains(pack_pair(r, a))) {
                continue;
            }
            root = r;
        } else {
            continue;
        }

        const int slot = get_or_create_island(root);
        IslandBuilder& island = islands[static_cast<std::size_t>(slot)];
        island.pairs.push_back(i);

        const ecs::Entity ent_a = entities[static_cast<std::size_t>(a)];
        const ecs::Entity ent_b = entities[static_cast<std::size_t>(b)];
        const ecs::Entity low_any = ent_a < ent_b ? ent_a : ent_b;
        if (!island.has_any || low_any < island.lowest_any) {
            island.lowest_any = low_any;
            island.has_any = true;
        }
        if (dyn_a && (!island.has_dynamic || ent_a < island.lowest_dynamic)) {
            island.lowest_dynamic = ent_a;
            island.has_dynamic = true;
        }
        if (dyn_b && (!island.has_dynamic || ent_b < island.lowest_dynamic)) {
            island.lowest_dynamic = ent_b;
            island.has_dynamic = true;
        }
    }

    // Count unique dynamic bodies per island.
    for (int body = 0; body < body_count; ++body) {
        if (used[static_cast<std::size_t>(body)] == 0 || !is_dyn(body)) {
            continue;
        }
        const int root = find(body);
        const auto found = island_of.find(root);
        if (found != island_of.end()) {
            ++islands[static_cast<std::size_t>(found->second)].dynamic_body_count;
        }
    }

    std::vector<int> order(islands.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int lhs_idx, int rhs_idx) {
        const IslandBuilder& lhs = islands[static_cast<std::size_t>(lhs_idx)];
        const IslandBuilder& rhs = islands[static_cast<std::size_t>(rhs_idx)];
        if (lhs.has_dynamic && rhs.has_dynamic && lhs.lowest_dynamic != rhs.lowest_dynamic) {
            return lhs.lowest_dynamic < rhs.lowest_dynamic;
        }
        if (lhs.has_any && rhs.has_any && lhs.lowest_any != rhs.lowest_any) {
            return lhs.lowest_any < rhs.lowest_any;
        }
        return lhs_idx < rhs_idx;
    });

    std::vector<ContactIsland> result;
    result.reserve(order.size());
    for (int index : order) {
        IslandBuilder& src = islands[static_cast<std::size_t>(index)];
        result.push_back(ContactIsland{
                .pairs = std::move(src.pairs),
                .dynamic_body_count = src.dynamic_body_count,
        });
    }
    return result;
}

std::vector<std::vector<int>> group_contact_islands(std::span<const ecs::Entity> entities,
                                                    std::span<const IslandBodyPair> links,
                                                    std::span<const std::uint8_t> overlapping,
                                                    std::span<const std::uint8_t> dynamic_body) {
    std::vector<ContactIsland> detailed = build_contact_islands(entities, links, overlapping, dynamic_body);
    std::vector<std::vector<int>> result;
    result.reserve(detailed.size());
    for (ContactIsland& island : detailed) {
        result.push_back(std::move(island.pairs));
    }
    return result;
}

}
