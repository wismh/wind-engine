#include "contact_graph_coloring.h"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace engine {

ContactColorGraph build_contact_color_graph(int body_count,
                                            std::span<const std::uint8_t> writable_body,
                                            std::span<const IslandBodyPair> constraints,
                                            int max_colors) {
    ContactColorGraph graph;
    if (constraints.empty() || body_count <= 0) {
        return graph;
    }

    const int capped_colors = std::clamp(max_colors, 1, 64);
    graph.colors.reserve(static_cast<std::size_t>(std::min(capped_colors, 16)));
    const bool has_writable_mask = (static_cast<int>(writable_body.size()) == body_count);

    auto is_writable = [&](int body) noexcept {
        if (body < 0 || body >= body_count) {
            return false;
        }
        if (!has_writable_mask) {
            return true;
        }
        return writable_body[static_cast<std::size_t>(body)] != 0;
    };

    std::vector<std::uint64_t> body_color_mask(static_cast<std::size_t>(body_count), 0ull);
    const int constraint_count = static_cast<int>(constraints.size());

    for (int i = 0; i < constraint_count; ++i) {
        const int a = constraints[static_cast<std::size_t>(i)].a;
        const int b = constraints[static_cast<std::size_t>(i)].b;
        if (a < 0 || b < 0 || a >= body_count || b >= body_count || a == b) {
            continue;
        }

        const bool write_a = is_writable(a);
        const bool write_b = is_writable(b);
        const std::uint64_t used_mask =
                (write_a ? body_color_mask[static_cast<std::size_t>(a)] : 0ull) |
                (write_b ? body_color_mask[static_cast<std::size_t>(b)] : 0ull);

        const int color = std::countr_one(used_mask);
        if (color >= capped_colors) {
            graph.overflow.push_back(i);
            continue;
        }

        const std::uint64_t bit = 1ull << static_cast<unsigned>(color);
        if (write_a) {
            body_color_mask[static_cast<std::size_t>(a)] |= bit;
        }
        if (write_b) {
            body_color_mask[static_cast<std::size_t>(b)] |= bit;
        }

        if (static_cast<int>(graph.colors.size()) <= color) {
            graph.colors.resize(static_cast<std::size_t>(color + 1));
        }
        graph.colors[static_cast<std::size_t>(color)].push_back(i);
    }

    return graph;
}

}
