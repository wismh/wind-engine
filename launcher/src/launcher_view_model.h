#pragma once

#include "method_command.h"
#include "project_row_view_model.h"
#include "sdk_row_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace launcher {

// Fields of assets/ui/launcher.xml. Names are the XML binding paths, so they stay camelCase.
class LauncherViewModel final : public engine::ui::ViewModel {
public:
    LauncherViewModel();

    engine::ui::BindableList<std::shared_ptr<ProjectRowViewModel>> projects;
    engine::ui::BindableList<std::shared_ptr<SdkRowViewModel>> sdks;
    engine::ui::Bindable<std::string> statusText;

    MethodCommand addProject;
    MethodCommand locateSdk;
};

}
