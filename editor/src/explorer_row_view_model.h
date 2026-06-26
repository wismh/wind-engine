#pragma once

#include "project_entry.h"

#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
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

    void show(const ProjectEntry& entry, const engine::ui::TreeRowInfo& tree, bool selected, bool drop,
            bool renaming);
    void set_fill(bool selected, bool drop);
    // The name in the rename field, once, when renaming starts.
    void seed_edit(const std::string& name);

    [[nodiscard]] const std::string& key() const;
    [[nodiscard]] const engine::ui::TreeRowInfo& tree() const;

    void select_row();
    void toggle_row();
    [[nodiscard]] bool can_toggle_row() const;
    void commit_rename();
    void new_folder();
    void new_world();
    void new_header();
    void new_source();
    void cut_row();
    void copy_row();
    void paste_row();
    [[nodiscard]] bool can_paste_row() const;
    void rename_row();
    void remove_row();

    engine::ui::Bindable<std::string> label;
    engine::ui::Bindable<int> depth;
    engine::ui::Bindable<bool> expanded;
    engine::ui::Bindable<std::string> rowFill;
    engine::ui::Bindable<std::string> rowDisplay;
    engine::ui::Bindable<std::string> renameDisplay;
    engine::ui::Bindable<std::string> editText;
    engine::ui::Bindable<bool> menuOpen;
    engine::ui::MethodCommand select;
    engine::ui::MethodCommand toggle;
    engine::ui::MethodCommand commitRename;
    engine::ui::MethodCommand newFolder;
    engine::ui::MethodCommand newWorld;
    engine::ui::MethodCommand newHeader;
    engine::ui::MethodCommand newSource;
    engine::ui::MethodCommand cut;
    engine::ui::MethodCommand copy;
    engine::ui::MethodCommand paste;
    engine::ui::MethodCommand rename;
    engine::ui::MethodCommand remove;

private:
    ExplorerPanel* panel_;
    std::string key_;
    engine::ui::TreeRowInfo tree_;
};

}
