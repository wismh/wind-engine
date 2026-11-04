#pragma once

// docs/tech/modules/ECS.md

#include <array>
#include <cstddef>

namespace engine::ecs {

enum class Schedule {
    Fixed,
    Frame,
};

// `NetReceive` opens both schedules and `NetSend` closes `Fixed`, so a network tick brackets the whole
// simulation step (docs/tech/modules/Net.md).
enum class Phase {
    NetReceive,
    Physics,
    Input,
    Game,
    Transform,
    Bind,
    Audio,
    Render,
    UiRender,
    NetSend,
};

inline constexpr std::size_t kScheduleCount = 2;
inline constexpr std::size_t kPhaseCount = 10;

inline constexpr std::array<Phase, 4> kFixedPhases{Phase::NetReceive, Phase::Physics, Phase::Game, Phase::NetSend};
inline constexpr std::array<Phase, 8> kFramePhases{Phase::NetReceive, Phase::Input, Phase::Game, Phase::Transform,
        Phase::Bind, Phase::Audio, Phase::Render, Phase::UiRender};

[[nodiscard]] constexpr std::size_t schedule_index(Schedule schedule) noexcept {
    return static_cast<std::size_t>(schedule);
}

[[nodiscard]] constexpr std::size_t phase_index(Phase phase) noexcept {
    return static_cast<std::size_t>(phase);
}

}
