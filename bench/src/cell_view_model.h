#pragma once

#include "arc_paint.h"

#include <engine/ui/view_model.h>

namespace bench {

// One generated cell of the paint-mix and motion grids. Only the arc cell binds anything (`arc`); the others are
// styled by CSS alone. Registered by hand because four documents share it.
class CellViewModel final : public engine::ui::ViewModel {
public:
    CellViewModel();

    ArcPaint arc;
};

}
