#pragma once

#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/inspector.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

class UiTreePanel;

// One row of the UI tree (`rows` in assets/ui/ui_tree.xml). Keeps a copy of the engine's row,
// which is plain data: nothing in it is dereferenced, and the panel drops every row on Stop.
class UiTreeRowViewModel final : public engine::ui::ViewModel {
public:
    explicit UiTreeRowViewModel(UiTreePanel& panel);

    UiTreeRowViewModel(const UiTreeRowViewModel&) = delete;
    UiTreeRowViewModel& operator=(const UiTreeRowViewModel&) = delete;

    void show(engine::ui::InspectorTreeRow row);
    [[nodiscard]] const engine::ui::InspectorTreeRow& row() const;

    // The row button: select this element in the game.
    void select_row();
    // The expander checkbox: collapse or expand. Disabled on a leaf.
    void toggle_row();
    [[nodiscard]] bool can_toggle_row() const;

    engine::ui::Bindable<std::string> label;
    // Tree depth through `var-depth`: the row's indent.
    engine::ui::Bindable<int> depth;
    // The expander's `checked`: the chevron turns down while the node is expanded.
    engine::ui::Bindable<bool> expanded;
    // Row background through `var-row`: the selected row is green.
    engine::ui::Bindable<std::string> rowFill;
    MethodCommand select;
    MethodCommand toggle;

private:
    UiTreePanel* panel_;
    engine::ui::InspectorTreeRow row_;
};

}
