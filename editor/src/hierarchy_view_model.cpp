#include "hierarchy_view_model.h"

#include <asset_ids.h>

namespace editor {

HierarchyViewModel::HierarchyViewModel() {
    assets::ui::Hierarchy::bind(*this);
}

}
