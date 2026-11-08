#include <engine/nav/flow_field_3d.h>

#include <glm/geometric.hpp>

#include <array>
#include <queue>

namespace engine::nav {

void FlowField3D::init_grid(glm::ivec3 origin, glm::ivec3 span) {
    origin_ = origin;
    span_ = span;
    const std::size_t total = static_cast<std::size_t>(span_.x * span_.y * span_.z);
    cells_.assign(total, CellData{});
    touched_.clear();
}

void FlowField3D::reset() noexcept {
    for (int idx : touched_) {
        cells_[static_cast<std::size_t>(idx)] = CellData{};
    }
    touched_.clear();
}

void FlowField3D::build(
        std::span<const glm::ivec3> goals,
        CellPredicate<glm::ivec3> can_stand,
        int max_step_up,
        int max_drop,
        float max_range) {
    reset();

    struct QueueEntry {
        float cost = 0.0f;
        int idx = 0;

        bool operator>(const QueueEntry& other) const noexcept {
            return cost > other.cost;
        }
    };

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;

    for (const glm::ivec3& goal : goals) {
        const int idx = cell_index(goal);
        if (idx < 0 || !can_stand(goal)) {
            continue;
        }
        cells_[static_cast<std::size_t>(idx)].cost = 0.0f;
        cells_[static_cast<std::size_t>(idx)].next = goal;
        cells_[static_cast<std::size_t>(idx)].direction = glm::vec3{0.0f};
        touched_.push_back(idx);
        queue.push(QueueEntry{.cost = 0.0f, .idx = idx});
    }

    static constexpr std::array<glm::ivec2, 8> kOffsets{
            glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1}, glm::ivec2{0, -1},
            glm::ivec2{1, 1}, glm::ivec2{1, -1}, glm::ivec2{-1, 1}, glm::ivec2{-1, -1}};

    while (!queue.empty()) {
        const QueueEntry top = queue.top();
        queue.pop();

        if (top.cost > cells_[static_cast<std::size_t>(top.idx)].cost) {
            continue;
        }
        if (top.cost >= max_range) {
            continue;
        }

        const glm::ivec3 target = cell_at(top.idx);

        for (const auto& offset : kOffsets) {
            const bool is_diagonal = (offset.x != 0 && offset.y != 0);
            const float base_cost = is_diagonal ? 1.41421356f : 1.0f;

            // Probe what standable cells 'from' could step into 'target':
            // 1. Level step: from.y == target.y
            const auto relax_step = [&](glm::ivec3 from, float step_cost) {
                const int from_idx = cell_index(from);
                if (from_idx < 0 || !can_stand(from)) {
                    return;
                }
                if (is_diagonal) {
                    const glm::ivec3 side_x{from.x, from.y, target.z};
                    const glm::ivec3 side_z{target.x, from.y, from.z};
                    if (!can_stand(side_x) || !can_stand(side_z)) {
                        return;
                    }
                }

                const float new_cost = top.cost + step_cost;
                CellData& from_cell = cells_[static_cast<std::size_t>(from_idx)];
                if (new_cost < from_cell.cost) {
                    if (from_cell.cost >= 1e8f) {
                        touched_.push_back(from_idx);
                    }
                    from_cell.cost = new_cost;
                    from_cell.next = target;
                    const glm::vec3 delta = glm::vec3{target - from};
                    from_cell.direction = (glm::length(delta) > 1e-4f) ? glm::normalize(delta) : glm::vec3{0.0f};
                    queue.push(QueueEntry{.cost = new_cost, .idx = from_idx});
                }
            };

            // Level: from is at target - offset
            relax_step(glm::ivec3{target.x - offset.x, target.y, target.z - offset.y}, base_cost);

            // from stepped up into target: from.y = target.y - dy
            for (int dy = 1; dy <= max_step_up; ++dy) {
                relax_step(glm::ivec3{target.x - offset.x, target.y - dy, target.z - offset.y},
                           base_cost + static_cast<float>(dy) * 0.5f);
            }

            // from dropped down into target: from.y = target.y + dy
            for (int dy = 1; dy <= max_drop; ++dy) {
                relax_step(glm::ivec3{target.x - offset.x, target.y + dy, target.z - offset.y},
                           base_cost + static_cast<float>(dy) * 0.2f);
            }
        }
    }
}

bool FlowField3D::has_flow(glm::ivec3 cell) const noexcept {
    const int idx = cell_index(cell);
    return idx >= 0 && cells_[static_cast<std::size_t>(idx)].cost < 1e8f;
}

glm::vec3 FlowField3D::direction(glm::ivec3 cell) const noexcept {
    const int idx = cell_index(cell);
    if (idx < 0 || cells_[static_cast<std::size_t>(idx)].cost >= 1e8f) {
        return glm::vec3{0.0f};
    }
    return cells_[static_cast<std::size_t>(idx)].direction;
}

std::optional<glm::ivec3> FlowField3D::next_cell(glm::ivec3 cell) const noexcept {
    const int idx = cell_index(cell);
    if (idx < 0 || cells_[static_cast<std::size_t>(idx)].cost >= 1e8f) {
        return std::nullopt;
    }
    return cells_[static_cast<std::size_t>(idx)].next;
}

float FlowField3D::cost(glm::ivec3 cell) const noexcept {
    const int idx = cell_index(cell);
    if (idx < 0) {
        return 1e9f;
    }
    return cells_[static_cast<std::size_t>(idx)].cost;
}

std::vector<glm::ivec3> FlowField3D::path_to_goal(glm::ivec3 start, std::size_t max_steps) const {
    std::vector<glm::ivec3> path;
    glm::ivec3 current = start;

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
