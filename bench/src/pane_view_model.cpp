#include "pane_view_model.h"

#include <asset_ids.h>

namespace bench {

PaneViewModel::PaneViewModel() {
    assets::ui::Clip::PaneRows::Panes::bind(*this);
}

}
