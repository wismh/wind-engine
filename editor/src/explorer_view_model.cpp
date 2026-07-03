#include "explorer_view_model.h"

#include <asset_ids.h>

namespace editor {

ExplorerViewModel::ExplorerViewModel() {
    assets::ui::Explorer::bind(*this);
}

}
