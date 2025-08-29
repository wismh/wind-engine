#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

// One line of an Inspector section (`lines` in assets/ui/inspector.xml).
class InspectorLineViewModel final : public engine::ui::ViewModel {
public:
    InspectorLineViewModel();

    engine::ui::Bindable<std::string> text;
};

}
