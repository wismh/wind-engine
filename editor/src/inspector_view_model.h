#pragma once

#include "inspector_row_view_model.h"
#include "rule_line_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

// Fields of assets/ui/inspector.xml. Names are the XML binding paths, so they stay camelCase.
class InspectorViewModel final : public engine::ui::ViewModel {
public:
    InspectorViewModel();

    engine::ui::BindableList<std::shared_ptr<InspectorRowViewModel>> rows;
    engine::ui::Bindable<std::string> detail;
    engine::ui::BindableList<std::shared_ptr<RuleLineViewModel>> rules;
    // Two-way with the game world's UiInspector::pick_pointer.
    engine::ui::Bindable<bool> pick;
    engine::ui::Bindable<std::string> hint;
};

}
