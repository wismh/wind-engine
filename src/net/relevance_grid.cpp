#include "net/relevance_grid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <tuple>

namespace engine::net {

namespace {

[[nodiscard]] bool cell_less(const glm::ivec3& a, const glm::ivec3& b) noexcept {
    return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
}

} // namespace

glm::ivec3 RelevanceGrid::cell_of(const glm::vec3& position, float cell_size) noexcept {
    return glm::ivec3(static_cast<int>(std::floor(position.x / cell_size)),
            static_cast<int>(std::floor(position.y / cell_size)),
            static_cast<int>(std::floor(position.z / cell_size)));
}

int RelevanceGrid::distance(const glm::ivec3& a, const glm::ivec3& b) noexcept {
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

void RelevanceGrid::rebuild(std::vector<Item> items) {
    items_ = std::move(items);
    std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return cell_less(a.cell, b.cell); });
    cells_.clear();
    for (std::uint32_t i = 0; i < items_.size(); ++i) {
        if (cells_.empty() || cells_.back().cell != items_[i].cell) {
            cells_.push_back(Cell{.cell = items_[i].cell, .first = i, .count = 0});
        }
        ++cells_.back().count;
    }
}

void RelevanceGrid::mark_cell(const Cell& cell, std::vector<std::uint8_t>& marks) const {
    for (std::uint32_t i = 0; i < cell.count; ++i) {
        marks[items_[cell.first + i].index] = 1;
    }
}

void RelevanceGrid::mark_around(const glm::ivec3& center, int radius, std::vector<std::uint8_t>& marks) const {
    const auto side = static_cast<std::int64_t>(radius) * 2 + 1;
    if (static_cast<std::int64_t>(cells_.size()) <= side * side * side) {
        mark_by_scanning(center, radius, marks);
    } else {
        mark_by_probing(center, radius, marks);
    }
}

void RelevanceGrid::mark_by_probing(const glm::ivec3& center, int radius, std::vector<std::uint8_t>& marks) const {
    for (int x = center.x - radius; x <= center.x + radius; ++x) {
        for (int y = center.y - radius; y <= center.y + radius; ++y) {
            for (int z = center.z - radius; z <= center.z + radius; ++z) {
                const glm::ivec3 wanted(x, y, z);
                const auto at = std::lower_bound(cells_.begin(), cells_.end(), wanted,
                        [](const Cell& cell, const glm::ivec3& value) { return cell_less(cell.cell, value); });
                if (at != cells_.end() && at->cell == wanted) {
                    mark_cell(*at, marks);
                }
            }
        }
    }
}

void RelevanceGrid::mark_by_scanning(const glm::ivec3& center, int radius, std::vector<std::uint8_t>& marks) const {
    for (const Cell& cell : cells_) {
        if (distance(cell.cell, center) <= radius) {
            mark_cell(cell, marks);
        }
    }
}

} // namespace engine::net
