#include <gtest/gtest.h>

#include "editor_selection.h"
#include "ui_tree_panel.h"

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

// A game world with one canvas: Canvas > Button#go > Label#lab, and a rule for the label.
struct Game {
    Game() {
        engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = {800, 600};
        auto document = engine::ui::parse_xml(R"(<Canvas><Button id="go"><Label id="lab">Go</Label></Button></Canvas>)");
        EXPECT_TRUE(document.has_value());
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css("Label { color: #ffffff; }\n#go > Label { color: #ff0000; }\n", warnings);
        EXPECT_TRUE(sheet.has_value());
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.rect = {0.0f, 0.0f, 800.0f, 600.0f};
        entity = engine::ui::spawn_canvas(world, canvas, std::move(*document), std::move(*sheet));
    }

    engine::ecs::World world;
    engine::ecs::Entity entity{};
};

std::vector<std::string> labels(const editor::UiTreeViewModel& vm) {
    std::vector<std::string> out;
    for (const auto& row : vm.rows.get()) {
        out.push_back(row->label.get());
    }
    return out;
}

}

TEST(UiTreePanel, IdleShowsAHintAndNoRows) {
    editor::EditorSelection selection;
    editor::UiTreePanel panel{selection};
    panel.refresh();
    const editor::UiTreeViewModel& vm = *panel.view_model();
    EXPECT_FALSE(panel.attached());
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_FALSE(vm.pick.get());
    EXPECT_NE(vm.hint.get().find("Play"), std::string::npos);
}

TEST(UiTreePanel, TreeRowsBecomeViewModelRows) {
    Game game;
    editor::EditorSelection selection;
    editor::UiTreePanel panel{selection};
    panel.attach(game.world);
    EXPECT_TRUE(engine::ui::inspector_attached(game.world));
    panel.refresh();

    const editor::UiTreeViewModel& vm = *panel.view_model();
    EXPECT_EQ(labels(vm), (std::vector<std::string>{"Canvas", "Button #go", "Label #lab"}));
    EXPECT_EQ(vm.rows.get()[0]->depth.get(), 0);
    EXPECT_EQ(vm.rows.get()[2]->depth.get(), 2) << "the indent is the depth, not spaces in the label";
    EXPECT_TRUE(vm.rows.get()[0]->expanded.get());
    EXPECT_FALSE(vm.rows.get()[2]->expanded.get());
    EXPECT_FALSE(vm.rows.get()[2]->toggle.can_execute()) << "a leaf has no expander";
    EXPECT_EQ(vm.rows.get()[2]->rowFill.get(), "#00000000");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(selection.target())) << "attaching selects nothing";

    const std::shared_ptr<editor::UiTreeRowViewModel> first = vm.rows.get()[0];
    panel.refresh();
    EXPECT_EQ(panel.view_model()->rows.get()[0], first) << "a row keeps its view-model across refreshes";
    panel.detach();
}

TEST(UiTreePanel, RowCommandSelectsAndBecomesTheEditorSelection) {
    Game game;
    editor::EditorSelection selection;
    editor::UiTreePanel panel{selection};
    panel.attach(game.world);
    panel.refresh();

    panel.view_model()->rows.get()[2]->select.execute();
    EXPECT_TRUE(engine::ui::inspector_selection(game.world).active);
    panel.refresh();

    const editor::UiTreeViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.rows.get()[2]->rowFill.get(), "#2f5d3a");
    EXPECT_EQ(vm.rows.get()[1]->rowFill.get(), "#00000000");
    EXPECT_EQ(selection.target(), editor::SelectionTarget{editor::UiElementSelection{}});

    // Another panel selects; the same row again takes the selection back.
    selection.select(editor::AssetSelection{.key = "a.txt"});
    panel.refresh();
    EXPECT_TRUE(std::holds_alternative<editor::AssetSelection>(selection.target())) << "a refresh keeps it";
    panel.view_model()->rows.get()[2]->select.execute();
    panel.refresh();
    EXPECT_EQ(selection.target(), editor::SelectionTarget{editor::UiElementSelection{}});
    panel.detach();
}

TEST(UiTreePanel, PickInTheGameShowsInThePanelAndSelects) {
    Game game;
    editor::EditorSelection selection;
    editor::UiTreePanel panel{selection};
    panel.attach(game.world);
    // What a pick click in the game window does (handle_pointer while pick_pointer is on).
    engine::ui::inspector_select(game.world, engine::kPrimaryWindow,
            engine::ui::InspectorPick{.canvas = game.entity, .path = {0}});
    panel.refresh();

    const editor::UiTreeViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.rows.get()[1]->rowFill.get(), "#2f5d3a");
    EXPECT_EQ(selection.target(), editor::SelectionTarget{editor::UiElementSelection{}});

    // Hidden tab: only sync_selection runs, and it still takes a pick.
    selection.clear();
    engine::ui::inspector_select(game.world, engine::kPrimaryWindow,
            engine::ui::InspectorPick{.canvas = game.entity, .path = {0, 0}});
    panel.sync_selection();
    EXPECT_EQ(selection.target(), editor::SelectionTarget{editor::UiElementSelection{}});
    const std::uint64_t revision = selection.revision();
    panel.sync_selection();
    engine::ui::inspector_retarget(game.world);
    panel.sync_selection();
    EXPECT_EQ(selection.revision(), revision) << "no new selection, no new revision";
    panel.detach();
}

TEST(UiTreePanel, ExpanderCollapsesAndExpands) {
    Game game;
    editor::EditorSelection selection;
    editor::UiTreePanel panel{selection};
    panel.attach(game.world);
    panel.refresh();

    panel.view_model()->rows.get()[1]->toggle.execute();
    panel.refresh();
    EXPECT_EQ(labels(*panel.view_model()), (std::vector<std::string>{"Canvas", "Button #go"}));
    EXPECT_FALSE(panel.view_model()->rows.get()[1]->expanded.get());

    panel.view_model()->rows.get()[1]->toggle.execute();
    panel.refresh();
    EXPECT_EQ(panel.view_model()->rows.get().size(), 3u);
    panel.detach();
}

TEST(UiTreePanel, PickIsTwoWay) {
    Game game;
    editor::EditorSelection selection;
    editor::UiTreePanel panel{selection};
    panel.attach(game.world);
    panel.refresh();
    editor::UiTreeViewModel& vm = *panel.view_model();
    EXPECT_FALSE(vm.pick.get());
    EXPECT_FALSE(game.world.ctx<engine::ui::UiInspector>().pick_pointer);

    vm.pick = true;  // the checkbox click writes the view-model
    panel.refresh();
    EXPECT_TRUE(game.world.ctx<engine::ui::UiInspector>().pick_pointer);
    EXPECT_TRUE(vm.pick.get());

    game.world.ctx<engine::ui::UiInspector>().pick_pointer = false;
    panel.refresh();
    EXPECT_FALSE(vm.pick.get()) << "a change on the game side shows in the checkbox";
    panel.detach();
}

TEST(UiTreePanel, DetachDropsEverythingAndRowsGoInert) {
    Game game;
    editor::EditorSelection selection;
    editor::UiTreePanel panel{selection};
    panel.attach(game.world);
    panel.view_model()->pick = true;
    panel.refresh();
    const std::shared_ptr<editor::UiTreeRowViewModel> kept = panel.view_model()->rows.get()[2];
    kept->select.execute();
    panel.refresh();

    panel.detach();
    const editor::UiTreeViewModel& vm = *panel.view_model();
    EXPECT_FALSE(panel.attached());
    EXPECT_FALSE(engine::ui::inspector_attached(game.world));
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_FALSE(vm.pick.get());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(selection.target())) << "the element went with the game";
    EXPECT_FALSE(game.world.ctx<engine::ui::UiInspector>().pick_pointer);

    kept->select.execute();
    EXPECT_FALSE(engine::ui::inspector_selection(game.world).active) << "a stale row does nothing";
}

TEST(UiTreePanel, DetachKeepsAFileSelection) {
    Game game;
    editor::EditorSelection selection;
    editor::UiTreePanel panel{selection};
    panel.attach(game.world);
    selection.select(editor::AssetSelection{.key = "a.txt"});
    panel.detach();
    EXPECT_TRUE(std::holds_alternative<editor::AssetSelection>(selection.target()));
}

TEST(UiTreePanel, TreeKeysSelectCollapseAndExpand) {
    Game game;
    editor::EditorSelection selection;
    editor::UiTreePanel panel{selection};
    panel.attach(game.world);
    panel.refresh();
    using engine::ui::TreeNav;

    EXPECT_EQ(panel.navigate(TreeNav::Down), std::optional<std::size_t>{0}) << "nothing selected: the first row";
    panel.refresh();
    EXPECT_EQ(panel.view_model()->rows.get()[0]->rowFill.get(), "#2f5d3a");

    EXPECT_EQ(panel.navigate(TreeNav::Last), std::optional<std::size_t>{2});
    panel.refresh();
    EXPECT_EQ(panel.navigate(TreeNav::Left), std::optional<std::size_t>{1}) << "a leaf moves to its parent";
    panel.refresh();
    EXPECT_EQ(panel.view_model()->rows.get()[1]->rowFill.get(), "#2f5d3a");

    EXPECT_EQ(panel.navigate(TreeNav::Left), std::optional<std::size_t>{1}) << "an expanded row collapses";
    panel.refresh();
    EXPECT_EQ(labels(*panel.view_model()), (std::vector<std::string>{"Canvas", "Button #go"}));
    EXPECT_EQ(panel.navigate(TreeNav::Right), std::optional<std::size_t>{1}) << "and expands again";
    panel.refresh();
    EXPECT_EQ(panel.view_model()->rows.get().size(), 3u);
    EXPECT_EQ(panel.navigate(TreeNav::Right), std::optional<std::size_t>{2}) << "then steps into it";
    panel.detach();

    EXPECT_FALSE(panel.navigate(TreeNav::Down).has_value()) << "detached: no rows";
}
