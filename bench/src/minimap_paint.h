#pragma once

#include "unit_marker.h"

#include <engine/ui/paint.h>

#include <vector>

namespace bench {

// The HUD scene's minimap: every unit as a dot scaled from the world to the element, and the camera's frame.
class MinimapPaint final : public engine::ui::IPaint {
public:
    explicit MinimapPaint(std::vector<UnitMarker> units);

    void paint(engine::ui::IDrawList& list, const engine::render::Rect& content) override;

private:
    std::vector<UnitMarker> units_;
};

}
