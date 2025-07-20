#include "sdk_option_view_model.h"

#include "launcher_app.h"

#include <asset_ids.h>

namespace launcher {

SdkOptionViewModel::SdkOptionViewModel(LauncherApp& app, std::size_t index)
    : app_(&app)
    , index_(index) {
    assets::ui::Launcher::SdkOptions::bind(*this);
    pick.bind_to<SdkOptionViewModel, &SdkOptionViewModel::pick_sdk>(*this);
}

void SdkOptionViewModel::pick_sdk() {
    app_->pick_new_project_sdk(index_);
}

}
