#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

// Fields of assets/ui/export_dialog.xml. Names are the XML binding paths, so they stay camelCase.
class ExportViewModel final : public engine::ui::ViewModel {
public:
    ExportViewModel();

    engine::ui::Bindable<std::string> path;
    engine::ui::Bindable<bool> development;
    engine::ui::Bindable<std::string> message;
    engine::ui::Bindable<std::string> messageTone;

    engine::ui::MethodCommand browse;
    engine::ui::MethodCommand confirm;
    engine::ui::MethodCommand cancel;
};

}
