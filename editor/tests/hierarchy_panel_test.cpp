#include <gtest/gtest.h>

#include "command_history.h"
#include "editor_selection.h"
#include "hierarchy_panel.h"

#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world_serializer.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

// Each row's label, indented two spaces per depth.
std::vector<std::string> labels(const editor::HierarchyViewModel& vm) {
    std::vector<std::string> out;
    for (const auto& row : vm.rows.get()) {
        out.push_back(std::string(static_cast<std::size_t>(row->depth.get()) * 2, ' ') + row->label.get());
    }
    return out;
}

engine::ecs::Entity spawn_named(engine::ecs::World& world, std::string id) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ecs::EntityName>(entity, engine::ecs::EntityName{.id = std::move(id)});
    return entity;
}

}

TEST(HierarchyPanel, IdleShowsHintAndNoRows) {
    editor::EditorSelection selection;
    editor::HierarchyPanel panel{selection};
    panel.refresh();
    const editor::HierarchyViewModel& vm = *panel.view_model();
    EXPECT_FALSE(panel.attached());
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_EQ(vm.statusText.get(), "Open a .world file in Project, or press Play.");
    EXPECT_EQ(panel.selected(), engine::ecs::Entity{});
}

TEST(HierarchyPanel, DisplaysEditWorldHierarchyAndCameraBadges) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity cam = spawn_named(edit_world, "main_camera");
    edit_world.emplace<engine::ecs::Camera>(cam);
    edit_world.ctx<engine::ecs::ActiveCamera>().entity = cam;

    const engine::ecs::Entity aux_cam = spawn_named(edit_world, "minimap_cam");
    edit_world.emplace<engine::ecs::Camera>(aux_cam);

    const engine::ecs::Entity squad = spawn_named(edit_world, "squad");
    const engine::ecs::Entity hero = spawn_named(edit_world, "hero");
    const engine::ecs::Entity sword = spawn_named(edit_world, "sword");
    const engine::ecs::Entity unnamed = edit_world.create();
    ASSERT_TRUE(engine::ecs::set_parent(edit_world, hero, squad));
    ASSERT_TRUE(engine::ecs::set_parent(edit_world, sword, hero));

    editor::EditorSelection selection;
    editor::HierarchyPanel panel{selection};
    panel.set_edit_world(&edit_world, "level.world");
    panel.refresh();

    const editor::HierarchyViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.statusText.get(), "level.world  (6 entities)");
    EXPECT_EQ(labels(vm),
            (std::vector<std::string>{
                    "main_camera  [ActiveCamera]",
                    "minimap_cam  [Camera]",
                    "squad",
                    "  hero",
                    "    sword",
                    "entity_" + std::to_string(unnamed.index),
            }));
    EXPECT_FALSE(vm.rows.get()[0]->toggle.can_execute()) << "a leaf has no expander";
    EXPECT_TRUE(vm.rows.get()[2]->toggle.can_execute()) << "a parent can toggle";
    EXPECT_TRUE(vm.rows.get()[2]->expanded.get());

    const std::shared_ptr<editor::HierarchyRowViewModel> squad_row = vm.rows.get()[2];
    panel.refresh();
    EXPECT_EQ(panel.view_model()->rows.get()[2], squad_row) << "row view-models are reused by entity index";
}

TEST(HierarchyPanel, ToggleCollapsesAndExpandsSubtree) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity parent = spawn_named(edit_world, "parent");
    const engine::ecs::Entity child = spawn_named(edit_world, "child");
    const engine::ecs::Entity grandchild = spawn_named(edit_world, "grandchild");
    ASSERT_TRUE(engine::ecs::set_parent(edit_world, child, parent));
    ASSERT_TRUE(engine::ecs::set_parent(edit_world, grandchild, child));

    editor::EditorSelection selection;
    editor::HierarchyPanel panel{selection};
    panel.set_edit_world(&edit_world, "scene.world");
    panel.refresh();

    const editor::HierarchyViewModel& vm = *panel.view_model();
    ASSERT_EQ(vm.rows.get().size(), 3u);

    vm.rows.get()[0]->toggle.execute();
    EXPECT_EQ(labels(vm), (std::vector<std::string>{"parent"}));
    EXPECT_FALSE(vm.rows.get()[0]->expanded.get());

    vm.rows.get()[0]->toggle.execute();
    EXPECT_EQ(labels(vm), (std::vector<std::string>{"parent", "  child", "    grandchild"}));
    EXPECT_TRUE(vm.rows.get()[0]->expanded.get());
}

TEST(HierarchyPanel, SelectPublishesEntitySelectionToEditorSelection) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity a = spawn_named(edit_world, "alpha");
    const engine::ecs::Entity b = spawn_named(edit_world, "beta");

    editor::EditorSelection selection;
    editor::HierarchyPanel panel{selection};
    panel.set_edit_world(&edit_world, "scene.world");
    panel.refresh();

    const editor::HierarchyViewModel& vm = *panel.view_model();
    vm.rows.get()[1]->select.execute();
    EXPECT_EQ(panel.selected(), b);
    EXPECT_EQ(selection.target(), (editor::SelectionTarget{editor::EntitySelection{.entity = b}}));
    EXPECT_EQ(vm.rows.get()[1]->rowFill.get(), "#2f5d3a");
    EXPECT_EQ(vm.rows.get()[0]->rowFill.get(), "#00000000");

    const std::uint64_t rev = selection.revision();
    panel.select(b);
    EXPECT_GT(selection.revision(), rev);

    // Invalid entity is ignored.
    panel.select(engine::ecs::Entity{.index = 99, .generation = 1});
    EXPECT_EQ(panel.selected(), b);
    (void)a;
}

TEST(HierarchyPanel, TreeNavigationMovesExpandsAndCollapses) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity root1 = spawn_named(edit_world, "root1");
    const engine::ecs::Entity child1 = spawn_named(edit_world, "child1");
    const engine::ecs::Entity root2 = spawn_named(edit_world, "root2");
    ASSERT_TRUE(engine::ecs::set_parent(edit_world, child1, root1));

    editor::EditorSelection selection;
    editor::HierarchyPanel panel{selection};
    panel.set_edit_world(&edit_world, "scene.world");
    panel.refresh();
    using engine::ui::TreeNav;

    EXPECT_EQ(panel.navigate(TreeNav::Down), std::optional<std::size_t>{0});
    EXPECT_EQ(panel.selected(), root1);
    EXPECT_EQ(selection.target(), (editor::SelectionTarget{editor::EntitySelection{.entity = root1}}));

    // Left on expanded root1 collapses it.
    EXPECT_EQ(panel.navigate(TreeNav::Left), std::optional<std::size_t>{0});
    EXPECT_FALSE(panel.view_model()->rows.get()[0]->expanded.get());
    EXPECT_EQ(labels(*panel.view_model()), (std::vector<std::string>{"root1", "root2"}));

    // Right on collapsed root1 expands it again.
    EXPECT_EQ(panel.navigate(TreeNav::Right), std::optional<std::size_t>{0});
    EXPECT_TRUE(panel.view_model()->rows.get()[0]->expanded.get());
    EXPECT_EQ(labels(*panel.view_model()), (std::vector<std::string>{"root1", "  child1", "root2"}));

    // Right on expanded root1 steps into child1.
    EXPECT_EQ(panel.navigate(TreeNav::Right), std::optional<std::size_t>{1});
    EXPECT_EQ(panel.selected(), child1);

    // Left on leaf child1 moves to its parent root1.
    EXPECT_EQ(panel.navigate(TreeNav::Left), std::optional<std::size_t>{0});
    EXPECT_EQ(panel.selected(), root1);

    // Last goes to root2.
    EXPECT_EQ(panel.navigate(TreeNav::Last), std::optional<std::size_t>{2});
    EXPECT_EQ(panel.selected(), root2);
}

TEST(HierarchyPanel, AttachOverridesEditWorldAndClearsInvalidSelectionOnSwitchOrDestroy) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity edit_entity = spawn_named(edit_world, "edit_hero");

    engine::ecs::World game_world;
    const engine::ecs::Entity game_entity = spawn_named(game_world, "live_player");

    editor::EditorSelection selection;
    editor::HierarchyPanel panel{selection};
    panel.set_edit_world(&edit_world, "level.world");
    panel.refresh();
    panel.select(edit_entity);
    ASSERT_EQ(selection.target(), (editor::SelectionTarget{editor::EntitySelection{.entity = edit_entity}}));

    // Attaching Play Mode game world clears the Edit Mode entity selection and shows the live game world.
    panel.attach(game_world);
    EXPECT_TRUE(panel.attached());
    EXPECT_EQ(panel.selected(), engine::ecs::Entity{});
    EXPECT_TRUE(std::holds_alternative<std::monostate>(selection.target()));

    panel.refresh();
    EXPECT_EQ(panel.view_model()->statusText.get(), "Live game  (1 entities)");
    EXPECT_EQ(labels(*panel.view_model()), (std::vector<std::string>{"live_player"}));

    panel.select(game_entity);
    EXPECT_EQ(selection.target(), (editor::SelectionTarget{editor::EntitySelection{.entity = game_entity}}));

    // Destroying the selected entity in the active world clears the selection on refresh.
    const engine::ecs::Entity transient = spawn_named(game_world, "bullet");
    panel.refresh();
    panel.select(transient);
    ASSERT_EQ(panel.selected(), transient);
    game_world.destroy(transient);
    panel.refresh();
    EXPECT_EQ(panel.selected(), engine::ecs::Entity{});
    EXPECT_TRUE(std::holds_alternative<std::monostate>(selection.target()));

    // Detaching preserves a non-entity selection (e.g. AssetSelection) and falls back to the Edit Mode world.
    selection.select(editor::AssetSelection{.key = "notes.txt"});
    panel.detach();
    EXPECT_FALSE(panel.attached());
    EXPECT_TRUE(std::holds_alternative<editor::AssetSelection>(selection.target()));

    panel.refresh();
    EXPECT_EQ(panel.view_model()->statusText.get(), "level.world  (1 entities)");
    EXPECT_EQ(labels(*panel.view_model()), (std::vector<std::string>{"edit_hero"}));

    // Closing the Edit Mode world clears an EntitySelection.
    panel.select(edit_entity);
    panel.set_edit_world(nullptr);
    EXPECT_EQ(panel.selected(), engine::ecs::Entity{});
    EXPECT_TRUE(std::holds_alternative<std::monostate>(selection.target()));
    EXPECT_TRUE(panel.view_model()->rows.get().empty());
}

TEST(HierarchyPanel, DirtyStateDisplaysAsteriskInStatusText) {
    engine::ecs::World edit_world;
    spawn_named(edit_world, "hero");

    editor::EditorSelection selection;
    editor::HierarchyPanel panel{selection};
    panel.set_edit_world(&edit_world, "level.world", false);
    panel.refresh();
    EXPECT_FALSE(panel.is_dirty());
    EXPECT_EQ(panel.view_model()->statusText.get(), "level.world  (1 entities)");

    panel.set_edit_world(&edit_world, "level.world", true);
    EXPECT_TRUE(panel.is_dirty());
    EXPECT_EQ(panel.view_model()->statusText.get(), "level.world*  (1 entities)");

    panel.set_edit_world(&edit_world, "level.world", false);
    EXPECT_FALSE(panel.is_dirty());
    EXPECT_EQ(panel.view_model()->statusText.get(), "level.world  (1 entities)");
}

TEST(HierarchyPanel, RefreshAdoptsEntitySelectionAndClearsWhenEmpty) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity alpha = spawn_named(edit_world, "alpha");
    const engine::ecs::Entity beta = spawn_named(edit_world, "beta");

    editor::EditorSelection selection;
    editor::HierarchyPanel panel{selection};
    panel.set_edit_world(&edit_world, "scene.world");
    panel.refresh();

    selection.select(editor::EntitySelection{.entity = beta});
    panel.refresh();
    EXPECT_EQ(panel.selected(), beta);
    EXPECT_EQ(panel.view_model()->rows.get()[1]->rowFill.get(), "#2f5d3a");

    selection.select(editor::AssetSelection{.key = "notes.txt"});
    panel.refresh();
    EXPECT_EQ(panel.selected(), beta);

    selection.clear();
    panel.refresh();
    EXPECT_EQ(panel.selected(), engine::ecs::Entity{});
    EXPECT_EQ(panel.view_model()->rows.get()[1]->rowFill.get(), "#00000000");
    (void)alpha;
}



TEST(HierarchyPanel, CreateEntityAndChildEntity) {
    engine::ecs::World edit_world;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::HierarchyPanel panel{selection};
    panel.set_command_history(&history);
    panel.set_edit_world(&edit_world, "test.world");
    panel.refresh();

    panel.create_entity(std::nullopt);
    EXPECT_EQ(edit_world.alive_entities().size(), 1u);
    const engine::ecs::Entity root = panel.selected();
    EXPECT_TRUE(edit_world.valid(root));
    EXPECT_EQ(edit_world.get<engine::ecs::EntityName>(root).id, "Entity");
    EXPECT_TRUE(edit_world.try_get<engine::Transform>(root) != nullptr);
    EXPECT_EQ(panel.renaming(), root);
    EXPECT_TRUE(panel.rename_focus_pending());

    panel.commit_rename();
    EXPECT_EQ(panel.renaming(), engine::ecs::Entity{});

    panel.create_entity(root);
    EXPECT_EQ(edit_world.alive_entities().size(), 2u);
    const engine::ecs::Entity child = panel.selected();
    EXPECT_TRUE(edit_world.valid(child));
    EXPECT_EQ(edit_world.get<engine::ecs::EntityName>(child).id, "Child");
    const auto* parent_comp = edit_world.try_get<engine::Parent>(child);
    ASSERT_TRUE(parent_comp != nullptr);
    EXPECT_EQ(parent_comp->entity, root);

    panel.commit_rename();
    EXPECT_EQ(labels(*panel.view_model()), (std::vector<std::string>{"Entity", "  Child"}));
}

TEST(HierarchyPanel, RenameEntityCommitsAndUpdatesEntityName) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity player = spawn_named(edit_world, "OldPlayer");
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::HierarchyPanel panel{selection};
    panel.set_command_history(&history);
    panel.set_edit_world(&edit_world, "scene.world");
    panel.refresh();

    panel.begin_rename(player);
    EXPECT_EQ(panel.renaming(), player);
    const auto& rows = panel.view_model()->rows.get();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0]->rowDisplay.get(), "none");
    EXPECT_EQ(rows[0]->renameDisplay.get(), "flex");
    EXPECT_EQ(rows[0]->editText.get(), "OldPlayer");

    rows[0]->editText = "  NewPlayer  ";
    panel.commit_rename();
    EXPECT_EQ(panel.renaming(), engine::ecs::Entity{});
    EXPECT_EQ(edit_world.get<engine::ecs::EntityName>(player).id, "NewPlayer");
    EXPECT_EQ(panel.view_model()->rows.get()[0]->label.get(), "NewPlayer");
}

TEST(HierarchyPanel, DeleteEntityDestroysSubtreeAndClearsSelection) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity parent = spawn_named(edit_world, "parent");
    const engine::ecs::Entity child = spawn_named(edit_world, "child");
    ASSERT_TRUE(engine::ecs::set_parent(edit_world, child, parent));

    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::HierarchyPanel panel{selection};
    panel.set_command_history(&history);
    panel.set_edit_world(&edit_world, "scene.world");
    panel.select(child);
    panel.refresh();

    EXPECT_EQ(panel.selected(), child);
    panel.remove_entity(parent);

    EXPECT_FALSE(edit_world.valid(parent));
    EXPECT_FALSE(edit_world.valid(child));
    EXPECT_EQ(panel.selected(), engine::ecs::Entity{});
    EXPECT_TRUE(panel.view_model()->rows.get().empty());
}

TEST(HierarchyPanel, ReparentEntityPreservesWorldPositionAndRejectsCycles) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity root_a = spawn_named(edit_world, "root_a");
    const engine::ecs::Entity root_b = spawn_named(edit_world, "root_b");
    const engine::ecs::Entity child_a = spawn_named(edit_world, "child_a");
    edit_world.emplace<engine::Transform>(root_a, engine::Transform{.position = {10.0f, 0.0f, 0.0f}});
    edit_world.emplace<engine::Transform>(root_b, engine::Transform{.position = {20.0f, 0.0f, 0.0f}});
    edit_world.emplace<engine::Transform>(child_a, engine::Transform{.position = {2.0f, 0.0f, 0.0f}});
    ASSERT_TRUE(engine::ecs::set_parent(edit_world, child_a, root_a));

    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::HierarchyPanel panel{selection};
    panel.set_command_history(&history);
    panel.set_edit_world(&edit_world, "scene.world");
    panel.refresh();

    EXPECT_FALSE(panel.can_drop(root_a, root_a));
    EXPECT_FALSE(panel.can_drop(root_a, child_a));
    EXPECT_TRUE(panel.can_drop(child_a, root_b));
    EXPECT_TRUE(panel.can_drop(child_a, std::nullopt));

    panel.reparent(child_a, root_b);
    EXPECT_EQ(edit_world.get<engine::Parent>(child_a).entity, root_b);

    panel.reparent(child_a, std::nullopt);
    EXPECT_TRUE(edit_world.try_get<engine::Parent>(child_a) == nullptr);
}

TEST(HierarchyPanel, UndoRedoCreateDeleteRenameReparent) {
    engine::ecs::World edit_world;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::HierarchyPanel panel{selection};
    panel.set_command_history(&history);
    panel.set_edit_world(&edit_world, "scene.world");
    panel.refresh();

    // 1. Create
    panel.create_entity(std::nullopt);
    panel.commit_rename();
    EXPECT_EQ(edit_world.alive_entities().size(), 1u);

    // Undo create -> entity deleted
    EXPECT_TRUE(history.undo());
    panel.refresh();
    EXPECT_EQ(edit_world.alive_entities().size(), 0u);

    // Redo create -> entity restored
    EXPECT_TRUE(history.redo());
    panel.refresh();
    EXPECT_EQ(edit_world.alive_entities().size(), 1u);

    // 2. Rename
    const engine::ecs::Entity restored = edit_world.alive_entities()[0];
    panel.begin_rename(restored);
    panel.view_model()->rows.get()[0]->editText = "Hero";
    panel.commit_rename();
    EXPECT_EQ(edit_world.get<engine::ecs::EntityName>(restored).id, "Hero");

    // Undo rename -> "Entity"
    EXPECT_TRUE(history.undo());
    panel.refresh();
    EXPECT_EQ(edit_world.get<engine::ecs::EntityName>(restored).id, "Entity");

    // Redo rename -> "Hero"
    EXPECT_TRUE(history.redo());
    panel.refresh();
    EXPECT_EQ(edit_world.get<engine::ecs::EntityName>(restored).id, "Hero");

    // 3. Delete
    panel.remove_entity(restored);
    EXPECT_EQ(edit_world.alive_entities().size(), 0u);

    // Undo delete -> entity restored
    EXPECT_TRUE(history.undo());
    panel.refresh();
    EXPECT_EQ(edit_world.alive_entities().size(), 1u);
    const engine::ecs::Entity back = edit_world.alive_entities()[0];
    EXPECT_EQ(edit_world.get<engine::ecs::EntityName>(back).id, "Hero");
}

TEST(HierarchyPanel, DropHighlightSetsRowFill) {
    engine::ecs::World edit_world;
    const engine::ecs::Entity a = spawn_named(edit_world, "a");
    const engine::ecs::Entity b = spawn_named(edit_world, "b");
    editor::EditorSelection selection;
    editor::HierarchyPanel panel{selection};
    panel.set_edit_world(&edit_world, "scene.world");
    panel.refresh();

    panel.set_drop_highlight(b);
    EXPECT_EQ(panel.view_model()->rows.get()[0]->rowFill.get(), "#00000000");
    EXPECT_EQ(panel.view_model()->rows.get()[1]->rowFill.get(), "#2a4a6a");

    panel.set_drop_highlight(std::nullopt);
    EXPECT_EQ(panel.view_model()->rows.get()[1]->rowFill.get(), "#00000000");
    (void)a;
}
