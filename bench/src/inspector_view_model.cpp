#include "inspector_view_model.h"

#include <asset_ids.h>

namespace bench {

InspectorViewModel::InspectorViewModel() {
    assets::ui::Inspector::bind(*this);
}

}
