#pragma once

#include "explorer_row_view_model.h"

#include <engine/resources/asset_id.h>
#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

// Fields of assets/ui/explorer.xml. Names are the XML binding paths, so they stay camelCase.
class ExplorerViewModel final : public engine::ui::ViewModel {
public:
    ExplorerViewModel();

    // The project directory above the list, plus the open folder in the grid, or why there is no list.
    engine::ui::Bindable<std::string> rootText;
    engine::ui::BindableList<std::shared_ptr<ExplorerRowViewModel>> rows;
    engine::ui::BindableList<std::shared_ptr<ExplorerRowViewModel>> tiles;
    // One of the two lists is shown. `if` hides the other, so it is not laid out.
    engine::ui::Bindable<bool> treeShown;
    engine::ui::Bindable<bool> gridShown;
    engine::ui::Bindable<float> gridScroll;
    // The icon of the view the header button switches to: a grid while the tree is showing, a list in the grid.
    engine::ui::Bindable<engine::AssetId> viewIcon;
    engine::ui::MethodCommand toggleView;
    engine::ui::MethodCommand gridUp;
    // The menu of an empty part of the list. The tree writes into the project root; the grid writes into the
    // open folder.
    engine::ui::Bindable<bool> blankMenuOpen;
    engine::ui::MethodCommand blankNewFolder;
    engine::ui::MethodCommand blankNewWorld;
    engine::ui::MethodCommand blankNewHeader;
    engine::ui::MethodCommand blankNewSource;
    engine::ui::MethodCommand blankPaste;
};

}
