#include "editor_view_model.h"

#include <asset_ids.h>

namespace editor {

EditorViewModel::EditorViewModel() {
    assets::ui::Editor::bind(*this);
}

}
