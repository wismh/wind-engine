#pragma once

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
    Bind,
    Audio,
    Render,
    UiRender,
};

inline constexpr std::size_t kScheduleCount = 2;
inline constexpr std::size_t kPhaseCount = 7;

inline constexpr std::array<Phase, 2> kFixedPhases{Phase::Physics, Phase::Game};
inline constexpr std::array<Phase, 6> kFramePhases{
        Phase::Input, Phase::Game, Phase::Bind, Phase::Audio, Phase::Render, Phase::UiRender};

[[nodiscard]] constexpr std::size_t schedule_index(Schedule schedule) noexcept {
    return static_cast<std::size_t>(schedule);
}

[[nodiscard]] constexpr std::size_t phase_index(Phase phase) noexcept {
    return static_cast<std::size_t>(phase);
}

}
