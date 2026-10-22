#pragma once

#include <engine/ecs/entity.h>
#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/tree.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

class HierarchyPanel;

// One row of the scene hierarchy tree (`rows` in assets/ui/hierarchy.xml): an alive entity of the active world.
class HierarchyRowViewModel final : public engine::ui::ViewModel {
public:
    explicit HierarchyRowViewModel(HierarchyPanel& panel);

    HierarchyRowViewModel(const HierarchyRowViewModel&) = delete;
    HierarchyRowViewModel& operator=(const HierarchyRowViewModel&) = delete;

    void show(engine::ecs::Entity entity, std::string text, const engine::ui::TreeRowInfo& tree,
            bool selected, bool drop, bool renaming);
    void set_fill(bool selected, bool drop);
    void seed_edit(const std::string& name);

    [[nodiscard]] engine::ecs::Entity entity() const noexcept { return entity_; }
    [[nodiscard]] const engine::ui::TreeRowInfo& tree() const noexcept { return tree_; }

    // The row button: select this entity.
    void select_row();
    // The expander checkbox: collapse or expand. Disabled on a leaf entity.
    void toggle_row();
    [[nodiscard]] bool can_toggle_row() const;

    void commit_rename();
    void create_child();
    void rename_row();
    void remove_row();

    engine::ui::Bindable<std::string> label;
    // Tree depth through `var-depth`: the row's indent.
    engine::ui::Bindable<int> depth{0};
    // The expander's `checked`: the chevron turns down while the entity's children are expanded.
    engine::ui::Bindable<bool> expanded{true};
    // Row background through `var-row`: the selected row is highlighted.
    engine::ui::Bindable<std::string> rowFill;
    engine::ui::Bindable<std::string> rowDisplay;
    engine::ui::Bindable<std::string> renameDisplay;
    engine::ui::Bindable<std::string> editText;
    engine::ui::Bindable<bool> menuOpen{false};

    engine::ui::MethodCommand select;
    engine::ui::MethodCommand toggle;
    engine::ui::MethodCommand commitRename;
    engine::ui::MethodCommand createChild;
    engine::ui::MethodCommand rename;
    engine::ui::MethodCommand remove;

private:
    HierarchyPanel* panel_;
    engine::ecs::Entity entity_{};
    engine::ui::TreeRowInfo tree_{};
};

}
