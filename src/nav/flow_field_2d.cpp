#include <engine/nav/flow_field_2d.h>

#include <glm/geometric.hpp>

#include <array>
#include <queue>

namespace engine::nav {

void FlowField2D::init_grid(glm::ivec2 origin, glm::ivec2 span) {
    origin_ = origin;
    span_ = span;
    const std::size_t total = static_cast<std::size_t>(span_.x * span_.y);
    cells_.assign(total, CellData{});
    touched_.clear();
}

void FlowField2D::reset() noexcept {
    for (int idx : touched_) {
        cells_[static_cast<std::size_t>(idx)] = CellData{};
    }
    touched_.clear();
}

void FlowField2D::build(
        std::span<const glm::ivec2> goals,
        CellPredicate<glm::ivec2> is_walkable,
        float max_range,
        bool diagonals) {
    reset();

    struct QueueEntry {
        float cost = 0.0f;
        int idx = 0;

        bool operator>(const QueueEntry& other) const noexcept {
            return cost > other.cost;
        }
    };

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;

    for (const glm::ivec2& goal : goals) {
        const int idx = cell_index(goal);
        if (idx < 0 || !is_walkable(goal)) {
            continue;
        }
        cells_[static_cast<std::size_t>(idx)].cost = 0.0f;
        cells_[static_cast<std::size_t>(idx)].next = goal;
        cells_[static_cast<std::size_t>(idx)].direction = glm::vec2{0.0f};
        touched_.push_back(idx);
        queue.push(QueueEntry{.cost = 0.0f, .idx = idx});
    }

    static constexpr std::array<glm::ivec2, 4> kCardinals{
            glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1}, glm::ivec2{0, -1}};
    static constexpr std::array<glm::ivec2, 4> kDiagonals{
            glm::ivec2{1, 1}, glm::ivec2{1, -1}, glm::ivec2{-1, 1}, glm::ivec2{-1, -1}};
    constexpr float kDiagCost = 1.41421356f;

    while (!queue.empty()) {
        const QueueEntry top = queue.top();
        queue.pop();

        if (top.cost > cells_[static_cast<std::size_t>(top.idx)].cost) {
            continue;
        }
        if (top.cost >= max_range) {
            continue;
        }

        const glm::ivec2 curr = cell_at(top.idx);

        // Relax 4 cardinal neighbors
        for (const auto& offset : kCardinals) {
            const glm::ivec2 nbr = curr + offset;
            const int nbr_idx = cell_index(nbr);
            if (nbr_idx < 0 || !is_walkable(nbr)) {
                continue;
            }

            const float new_cost = top.cost + 1.0f;
            CellData& nbr_cell = cells_[static_cast<std::size_t>(nbr_idx)];
            if (new_cost < nbr_cell.cost) {
                if (nbr_cell.cost >= 1e8f) {
                    touched_.push_back(nbr_idx);
                }
                nbr_cell.cost = new_cost;
                nbr_cell.next = curr;
                nbr_cell.direction = glm::normalize(glm::vec2{curr - nbr});
                queue.push(QueueEntry{.cost = new_cost, .idx = nbr_idx});
            }
        }

        if (!diagonals) {
            continue;
        }

        // Relax 4 diagonal neighbors (with corner-cutting check)
        for (const auto& offset : kDiagonals) {
            const glm::ivec2 nbr = curr + offset;
            const int nbr_idx = cell_index(nbr);
            if (nbr_idx < 0 || !is_walkable(nbr)) {
                continue;
            }

            // Check adjacent orthogonal cells
            const glm::ivec2 side_x{curr.x + offset.x, curr.y};
            const glm::ivec2 side_y{curr.x, curr.y + offset.y};
            if (!is_walkable(side_x) || !is_walkable(side_y)) {
                continue;
            }

            const float new_cost = top.cost + kDiagCost;
            CellData& nbr_cell = cells_[static_cast<std::size_t>(nbr_idx)];
            if (new_cost < nbr_cell.cost) {
                if (nbr_cell.cost >= 1e8f) {
                    touched_.push_back(nbr_idx);
                }
                nbr_cell.cost = new_cost;
                nbr_cell.next = curr;
                nbr_cell.direction = glm::normalize(glm::vec2{curr - nbr});
                queue.push(QueueEntry{.cost = new_cost, .idx = nbr_idx});
            }
        }
    }
}

bool FlowField2D::has_flow(glm::ivec2 cell) const noexcept {
    const int idx = cell_index(cell);
    return idx >= 0 && cells_[static_cast<std::size_t>(idx)].cost < 1e8f;
}

glm::vec2 FlowField2D::direction(glm::ivec2 cell) const noexcept {
    const int idx = cell_index(cell);
    if (idx < 0 || cells_[static_cast<std::size_t>(idx)].cost >= 1e8f) {
        return glm::vec2{0.0f};
    }
    return cells_[static_cast<std::size_t>(idx)].direction;
}

std::optional<glm::ivec2> FlowField2D::next_cell(glm::ivec2 cell) const noexcept {
    const int idx = cell_index(cell);
    if (idx < 0 || cells_[static_cast<std::size_t>(idx)].cost >= 1e8f) {
        return std::nullopt;
    }
    return cells_[static_cast<std::size_t>(idx)].next;
}

float FlowField2D::cost(glm::ivec2 cell) const noexcept {
    const int idx = cell_index(cell);
    if (idx < 0) {
        return 1e9f;
    }
    return cells_[static_cast<std::size_t>(idx)].cost;
}

std::vector<glm::ivec2> FlowField2D::path_to_goal(glm::ivec2 start, std::size_t max_steps) const {
    std::vector<glm::ivec2> path;
    glm::ivec2 current = start;

    for (std::size_t step = 0; step < max_steps; ++step) {
        const int idx = cell_index(current);
        if (idx < 0) {
            break;
        }
        const CellData& data = cells_[static_cast<std::size_t>(idx)];
        if (data.cost >= 1e8f || data.next == current) {
            break;
        }
        path.push_back(data.next);
        current = data.next;
    }

    return path;
}

} // namespace engine::nav
