#pragma once

#include "unit_marker.h"

#include <engine/ui/paint.h>

#include <vector>

namespace bench {

// The HUD scene's world inside its Viewport: a grid every kWorldGrid px, a square per unit in its team's color, and a
// ring round the first few (the selection).
class WorldPaint final : public engine::ui::IPaint {
public:
    explicit WorldPaint(std::vector<UnitMarker> units);

    void paint(engine::ui::IDrawList& list, const engine::render::Rect& content) override;

private:
    std::vector<UnitMarker> units_;
};

}
