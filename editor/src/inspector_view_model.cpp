#include "inspector_view_model.h"

#include <asset_ids.h>

namespace editor {

InspectorViewModel::InspectorViewModel() {
    assets::ui::Inspector::bind(*this);
}

}
