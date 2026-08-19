#pragma once

#include "explorer_row_view_model.h"
#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

// Fields of assets/ui/explorer.xml. Names are the XML binding paths, so they stay camelCase.
class ExplorerViewModel final : public engine::ui::ViewModel {
public:
    ExplorerViewModel();

    // The project directory above the tree, or why there is no tree.
    engine::ui::Bindable<std::string> rootText;
    engine::ui::BindableList<std::shared_ptr<ExplorerRowViewModel>> rows;
    MethodCommand refresh;
};

}
