#include "hud_view_model.h"

#include <asset_ids.h>

namespace bench {

HudViewModel::HudViewModel(const std::vector<UnitMarker>& units)
    : world(units)
    , minimap(units) {
    assets::ui::Hud::bind(*this);
}

}
