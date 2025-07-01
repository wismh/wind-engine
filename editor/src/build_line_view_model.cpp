#include "build_line_view_model.h"

#include <asset_ids.h>

namespace editor {

BuildLineViewModel::BuildLineViewModel() {
    assets::ui::Build::Lines::bind(*this);
}

}
