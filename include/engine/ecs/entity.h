#pragma once

#include <cstdint>

namespace engine::ecs {

struct Entity {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    constexpr bool operator==(const Entity&) const noexcept = default;

    constexpr bool operator<(const Entity& other) const noexcept {
        if (index != other.index) {
            return index < other.index;
        }
        return generation < other.generation;
    }
};

}
