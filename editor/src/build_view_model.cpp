#include "build_view_model.h"

#include <asset_ids.h>

namespace editor {

BuildViewModel::BuildViewModel() {
    assets::ui::Build::bind(*this);
}

}
