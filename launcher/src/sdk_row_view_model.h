#pragma once

#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <cstddef>
#include <string>

namespace launcher {

class LauncherApp;

// One editor row (`sdks` in assets/ui/launcher.xml). Forget is enabled only for an SDK the user located.
class SdkRowViewModel final : public engine::ui::ViewModel {
public:
    SdkRowViewModel(LauncherApp& app, std::size_t index, bool forgettable);

    SdkRowViewModel(const SdkRowViewModel&) = delete;
    SdkRowViewModel& operator=(const SdkRowViewModel&) = delete;

    void forget_sdk();
    [[nodiscard]] bool can_forget_sdk() const;

    engine::ui::Bindable<std::string> version;
    // Configuration, dirty, short commit, and whether it was located or installed.
    engine::ui::Bindable<std::string> detail;
    engine::ui::Bindable<std::string> path;
    MethodCommand forget;

private:
    LauncherApp* app_;
    std::size_t index_;
    bool forgettable_;
};

}
