#include "launcher_view_model.h"

#include <asset_ids.h>

namespace launcher {

LauncherViewModel::LauncherViewModel() {
    assets::ui::Launcher::bind(*this);
}

}
