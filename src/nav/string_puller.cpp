#include <engine/nav/string_puller.h>

#include <cstdlib>

namespace engine::nav {

bool segment_clear_2d(
        glm::ivec2 from, glm::ivec2 to, CellPredicate<glm::ivec2> is_walkable) {
    if (!is_walkable(from) || !is_walkable(to)) {
        return false;
    }
    if (from == to) {
        return true;
    }

    glm::ivec2 cell = from;
    const int step_x = (cell.x < to.x) ? 1 : -1;
    const int step_y = (cell.y < to.y) ? 1 : -1;
    const int span_x = std::abs(to.x - cell.x);
    const int span_y = std::abs(to.y - cell.y);
    int error = span_x - span_y;

    while (cell != to) {
        const int double_error = error * 2;
        const bool advance_x = double_error > -span_y;
        const bool advance_y = double_error < span_x;

        if (advance_x && advance_y) {
            // Diagonal move: ensure adjacent sides are walkable to avoid cutting corners.
            const glm::ivec2 side_x{cell.x + step_x, cell.y};
            const glm::ivec2 side_y{cell.x, cell.y + step_y};
            if (!is_walkable(side_x) || !is_walkable(side_y)) {
                return false;
            }
            cell.x += step_x;
            cell.y += step_y;
            error += span_x - span_y;
        } else if (advance_x) {
            cell.x += step_x;
            error -= span_y;
        } else {
            cell.y += step_y;
            error += span_x;
        }

        if (!is_walkable(cell)) {
            return false;
        }
    }
    return true;
}

bool segment_clear_3d(
        glm::ivec3 from, glm::ivec3 to, CellPredicate<glm::ivec3> is_walkable) {
    if (from.y != to.y) {
        return false;
    }
    if (!is_walkable(from) || !is_walkable(to)) {
        return false;
    }
    if (from == to) {
        return true;
    }

    const int floor = from.y;
    glm::ivec3 cell = from;
    const int step_x = (cell.x < to.x) ? 1 : -1;
    const int step_z = (cell.z < to.z) ? 1 : -1;
    const int span_x = std::abs(to.x - cell.x);
    const int span_z = std::abs(to.z - cell.z);
    int error = span_x - span_z;

    while (cell != to) {
        const int double_error = error * 2;
        const bool advance_x = double_error > -span_z;
        const bool advance_z = double_error < span_x;

        if (advance_x && advance_z) {
            const glm::ivec3 side_x{cell.x + step_x, floor, cell.z};
            const glm::ivec3 side_z{cell.x, floor, cell.z + step_z};
            if (!is_walkable(side_x) || !is_walkable(side_z)) {
                return false;
            }
            cell.x += step_x;
            cell.z += step_z;
            error += span_x - span_z;
        } else if (advance_x) {
            cell.x += step_x;
            error -= span_z;
        } else {
            cell.z += step_z;
            error += span_x;
        }

        if (!is_walkable(cell)) {
            return false;
        }
    }
    return true;
}

std::vector<glm::ivec2> smooth_path_2d(
        std::span<const glm::ivec2> path,
        CellPredicate<glm::ivec2> is_walkable,
        std::size_t max_lookahead) {
    return smooth_path(path, [&](glm::ivec2 a, glm::ivec2 b) {
        return segment_clear_2d(a, b, is_walkable);
    }, max_lookahead);
}

std::vector<glm::ivec3> smooth_path_3d(
        std::span<const glm::ivec3> path,
        CellPredicate<glm::ivec3> is_walkable,
        std::size_t max_lookahead) {
    return smooth_path(path, [&](glm::ivec3 a, glm::ivec3 b) {
        return segment_clear_3d(a, b, is_walkable);
    }, max_lookahead);
}

} // namespace engine::nav
