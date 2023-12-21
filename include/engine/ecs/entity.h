#pragma once

// docs/tech/modules/ECS.md

#include <cstddef>
#include <cstdint>
#include <functional>

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

} // namespace engine::ecs

template <>
struct std::hash<engine::ecs::Entity> {
    constexpr std::size_t operator()(const engine::ecs::Entity& entity) const noexcept {
        return (static_cast<std::size_t>(entity.index) << 32) ^ static_cast<std::size_t>(entity.generation);
    }
};
