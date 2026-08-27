#pragma once

#include <glm/vec2.hpp>

namespace bench {

// One unit on the HUD scene's world map and minimap, in world pixels.
struct UnitMarker {
    glm::vec2 position{};
    int team = 0;
};

}
