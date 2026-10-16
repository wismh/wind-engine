#include "world_view_view_model.h"

#include <asset_ids.h>

namespace editor {

WorldViewViewModel::WorldViewViewModel() {
    assets::ui::WorldView::bind(*this);
}

}
