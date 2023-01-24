#pragma once

#include <cstdint>

namespace engine::ecs {

struct Entity {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    constexpr bool operator==(const Entity&) const noexcept = default;
};

}
