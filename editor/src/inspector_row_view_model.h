#pragma once

#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/inspector.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

class InspectorPanel;

// One row of the inspector tree (`rows` in assets/ui/inspector.xml). Keeps a copy of the engine's row,
// which is plain data: nothing in it is dereferenced, and the panel drops every row on Stop.
class InspectorRowViewModel final : public engine::ui::ViewModel {
public:
    explicit InspectorRowViewModel(InspectorPanel& panel);

    InspectorRowViewModel(const InspectorRowViewModel&) = delete;
    InspectorRowViewModel& operator=(const InspectorRowViewModel&) = delete;

    void show(engine::ui::InspectorTreeRow row);
    [[nodiscard]] const engine::ui::InspectorTreeRow& row() const;

    // The row button: select this element in the game.
    void select_row();
    // The twist button: collapse or expand.
    void toggle_row();
    [[nodiscard]] bool can_toggle_row() const;

    engine::ui::Bindable<std::string> label;
    engine::ui::Bindable<std::string> twist;
    // Row background through `var-row`: the selected row is green.
    engine::ui::Bindable<std::string> rowFill;
    MethodCommand select;
    MethodCommand toggle;

private:
    InspectorPanel* panel_;
    engine::ui::InspectorTreeRow row_;
};

}
