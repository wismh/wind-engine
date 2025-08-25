#pragma once

#include <engine/core/window_desc.h>

namespace editor {

// An element of the game's UI, selected in the UI Tree tab or by a pick click in the game. The element is
// the probe's selection of `window` (`inspector_selection`), which follows the element when its path moves;
// nothing here points into the game world.
struct UiElementSelection {
    engine::WindowId window = engine::kPrimaryWindow;

    bool operator==(const UiElementSelection&) const = default;
};

}
