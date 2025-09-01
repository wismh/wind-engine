#pragma once

#include "inspector_section_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

// Fields of assets/ui/inspector.xml. Names are the XML binding paths, so they stay camelCase.
class InspectorViewModel final : public engine::ui::ViewModel {
public:
    InspectorViewModel();

    // What is selected: a file name, or an element's `Kind #id .class`, or why nothing is shown.
    engine::ui::Bindable<std::string> title;
    // What kind of thing it is, or how to select something.
    engine::ui::Bindable<std::string> subtitle;
    engine::ui::BindableList<std::shared_ptr<InspectorSectionViewModel>> sections;
};

}
