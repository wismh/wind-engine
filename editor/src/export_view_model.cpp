#include "export_view_model.h"

#include <asset_ids.h>

namespace editor {

ExportViewModel::ExportViewModel() {
    assets::ui::ExportDialog::bind(*this);
}

}
