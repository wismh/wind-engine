#pragma once

#include "hierarchy_row_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

// Fields of assets/ui/hierarchy.xml. Names are the XML binding paths, so they stay camelCase.
class HierarchyViewModel final : public engine::ui::ViewModel {
public:
    HierarchyViewModel();

    engine::ui::Bindable<std::string> statusText;
    engine::ui::BindableList<std::shared_ptr<HierarchyRowViewModel>> rows;
};

}
