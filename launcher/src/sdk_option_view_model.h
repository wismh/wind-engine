#pragma once

#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <cstddef>
#include <string>

namespace launcher {

class LauncherApp;

// One SDK in the New project page's SDK picker (`sdkOptions` in assets/ui/launcher.xml). Picking it only changes
// view-model fields, so it calls the launcher at once instead of leaving a request.
class SdkOptionViewModel final : public engine::ui::ViewModel {
public:
    SdkOptionViewModel(LauncherApp& app, std::size_t index);

    SdkOptionViewModel(const SdkOptionViewModel&) = delete;
    SdkOptionViewModel& operator=(const SdkOptionViewModel&) = delete;

    void pick_sdk();

    // "0.1.0  Release, located", plus "no template" when the SDK cannot make projects.
    engine::ui::Bindable<std::string> text;
    engine::ui::Bindable<bool> selected;
    MethodCommand pick;

private:
    LauncherApp* app_;
    std::size_t index_;
};

}
