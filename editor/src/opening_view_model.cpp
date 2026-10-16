#include "opening_view_model.h"

#include <asset_ids.h>

namespace editor {

OpeningViewModel::OpeningViewModel() {
    assets::ui::Opening::bind(*this);
}

}
