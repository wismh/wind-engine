#pragma once

#include "method_command.h"
#include "project_entry.h"

#include <engine/ui/bindable.h>
#include <engine/ui/tree.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

class ExplorerPanel;

// One row of the project tree (`rows` in assets/ui/explorer.xml): a file or folder of the last scan.
class ExplorerRowViewModel final : public engine::ui::ViewModel {
public:
    explicit ExplorerRowViewModel(ExplorerPanel& panel);

    ExplorerRowViewModel(const ExplorerRowViewModel&) = delete;
    ExplorerRowViewModel& operator=(const ExplorerRowViewModel&) = delete;

    void show(const ProjectEntry& entry, const engine::ui::TreeRowInfo& tree, bool selected);
    [[nodiscard]] const std::string& key() const;
    [[nodiscard]] const engine::ui::TreeRowInfo& tree() const;

    // The row button: select this file or folder.
    void select_row();
    // The expander checkbox: collapse or expand. Disabled on a file and on an empty folder.
    void toggle_row();
    [[nodiscard]] bool can_toggle_row() const;

    engine::ui::Bindable<std::string> label;
    // Tree depth through `var-depth`: the row's indent.
    engine::ui::Bindable<int> depth;
    // The expander's `checked`: the chevron turns down while the folder is expanded.
    engine::ui::Bindable<bool> expanded;
    // Row background through `var-row`: the selected row is green.
    engine::ui::Bindable<std::string> rowFill;
    MethodCommand select;
    MethodCommand toggle;

private:
    ExplorerPanel* panel_;
    std::string key_;
    engine::ui::TreeRowInfo tree_;
};

}
