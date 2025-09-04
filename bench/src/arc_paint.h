#pragma once

#include <engine/ui/paint.h>

namespace bench {

// One stroked arc, three quarters of a ring, centred in the element: the paint-mix arc cell.
class ArcPaint final : public engine::ui::IPaint {
public:
    void paint(engine::ui::IDrawList& list, const engine::render::Rect& content) override;
};

}
