#pragma once

#include "ui_tree_row_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

// Fields of assets/ui/ui_tree.xml. Names are the XML binding paths, so they stay camelCase.
class UiTreeViewModel final : public engine::ui::ViewModel {
public:
    UiTreeViewModel();

    engine::ui::BindableList<std::shared_ptr<UiTreeRowViewModel>> rows;
    // Two-way with the game world's UiInspector::pick_pointer.
    engine::ui::Bindable<bool> pick;
    engine::ui::Bindable<std::string> hint;
};

}
