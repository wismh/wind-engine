#include "ui_tree_view_model.h"

#include <asset_ids.h>

namespace editor {

UiTreeViewModel::UiTreeViewModel() {
    assets::ui::UiTree::bind(*this);
}

}
