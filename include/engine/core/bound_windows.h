#pragma once

#include <engine/core/window_desc.h>

#include <vector>

namespace engine {

// Windows whose input, meshes, and UI belong to one world. `Worlds::bind_window` writes this.
// Empty means the world draws nothing and receives no device events.
struct BoundWindows {
    std::vector<WindowId> ids;
};

}
