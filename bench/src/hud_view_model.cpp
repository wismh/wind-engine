#include "hud_view_model.h"

#include <asset_ids.h>

#include <engine/ui/binding_id.h>

namespace bench {

HudViewModel::HudViewModel(const std::vector<UnitMarker>& units)
    : world(units)
    , minimap(units) {
    assets::ui::Hud::bind(*this);
    // The generated bind() registers properties and commands, not paints.
    paint(engine::ui::intern("world"), world);
    paint(engine::ui::intern("minimap"), minimap);
}

}
