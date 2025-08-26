#pragma once

#include <glm/vec4.hpp>

#include <array>

namespace bench {

// One color per HUD team (hud_data.h kTeams).
inline constexpr std::array<glm::vec4, 4> kTeamColors{
        glm::vec4{0.37f, 0.51f, 0.67f, 1.0f},
        glm::vec4{0.75f, 0.38f, 0.42f, 1.0f},
        glm::vec4{0.64f, 0.75f, 0.55f, 1.0f},
        glm::vec4{0.92f, 0.80f, 0.55f, 1.0f},
};

}
