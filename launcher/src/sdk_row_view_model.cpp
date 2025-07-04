#include "sdk_row_view_model.h"

#include "launcher_app.h"

#include <asset_ids.h>

namespace launcher {

SdkRowViewModel::SdkRowViewModel(LauncherApp& app, std::size_t index, bool forgettable)
    : app_(&app)
    , index_(index)
    , forgettable_(forgettable) {
    assets::ui::Launcher::Sdks::bind(*this);
    forget.bind_to<SdkRowViewModel, &SdkRowViewModel::forget_sdk, &SdkRowViewModel::can_forget_sdk>(*this);
}

void SdkRowViewModel::forget_sdk() {
    app_->request(LauncherRequest{.kind = LauncherRequest::Kind::Forget, .index = index_});
}

bool SdkRowViewModel::can_forget_sdk() const {
    return forgettable_;
}

}
