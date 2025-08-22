#include "inspector_line_view_model.h"

#include <asset_ids.h>

namespace editor {

InspectorLineViewModel::InspectorLineViewModel() {
    assets::ui::Inspector::Sections::Lines::bind(*this);
}

}
