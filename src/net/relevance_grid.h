#pragma once

// docs/tech/modules/Net.md#interest

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::net {

// Which captured entities sit in which cell, rebuilt once per send tick from their root positions.
class RelevanceGrid {
public:
    struct Item {
        glm::ivec3 cell{0};
        std::uint32_t index{0};
    };

    [[nodiscard]] static glm::ivec3 cell_of(const glm::vec3& position, float cell_size) noexcept;
    // Chebyshev distance in cells.
    [[nodiscard]] static int distance(const glm::ivec3& a, const glm::ivec3& b) noexcept;

    void rebuild(std::vector<Item> items);

    // Sets `marks[index] = 1` for every item within `radius` cells of `center`, by whichever is cheaper: probing each
    // cell of the cube or scanning the occupied cells.
    void mark_around(const glm::ivec3& center, int radius, std::vector<std::uint8_t>& marks) const;
    void mark_by_probing(const glm::ivec3& center, int radius, std::vector<std::uint8_t>& marks) const;
    void mark_by_scanning(const glm::ivec3& center, int radius, std::vector<std::uint8_t>& marks) const;

private:
    struct Cell {
        glm::ivec3 cell{0};
        std::uint32_t first{0};
        std::uint32_t count{0};
    };

    void mark_cell(const Cell& cell, std::vector<std::uint8_t>& marks) const;

    std::vector<Item> items_{};
    std::vector<Cell> cells_{};
};

} // namespace engine::net
