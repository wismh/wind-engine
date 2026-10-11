#pragma once

// docs/tech/modules/ECS.md

#include <array>
#include <cstddef>

namespace engine::ecs {

enum class Schedule {
    Fixed,
    Frame,
};

enum class Phase {
    Physics,
    Input,
    Game,
    Transform,
    Bind,
    Audio,
    Render,
    UiRender,
};

inline constexpr std::size_t kScheduleCount = 2;
inline constexpr std::size_t kPhaseCount = 8;

inline constexpr std::array<Phase, 2> kFixedPhases{Phase::Physics, Phase::Game};
inline constexpr std::array<Phase, 7> kFramePhases{
        Phase::Input, Phase::Game, Phase::Transform, Phase::Bind, Phase::Audio, Phase::Render, Phase::UiRender};

[[nodiscard]] constexpr std::size_t schedule_index(Schedule schedule) noexcept {
    return static_cast<std::size_t>(schedule);
}

[[nodiscard]] constexpr std::size_t phase_index(Phase phase) noexcept {
    return static_cast<std::size_t>(phase);
}

}
