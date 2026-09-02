#include "pane_row_view_model.h"

#include <asset_ids.h>

namespace bench {

PaneRowViewModel::PaneRowViewModel() {
    assets::ui::Clip::PaneRows::bind(*this);
}

}
