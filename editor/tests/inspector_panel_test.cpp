#include <gtest/gtest.h>

#include "inspector_panel.h"

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>

#include <memory>
#include <string>
#include <utility>
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

std::vector<std::string> labels(const editor::InspectorViewModel& vm) {
    std::vector<std::string> out;
    for (const auto& row : vm.rows.get()) {
        out.push_back(row->label.get());
    }
    return out;
}

}

TEST(InspectorPanel, IdleShowsAHintAndNoRows) {
    editor::InspectorPanel panel;
    panel.refresh();
    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_FALSE(panel.attached());
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_TRUE(vm.rules.get().empty());
    EXPECT_FALSE(vm.pick.get());
    EXPECT_NE(vm.hint.get().find("Play"), std::string::npos);
}

TEST(InspectorPanel, TreeRowsBecomeViewModelRows) {
    Game game;
    editor::InspectorPanel panel;
    panel.attach(game.world);
    EXPECT_TRUE(engine::ui::inspector_attached(game.world));
    panel.refresh();

    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_EQ(labels(vm), (std::vector<std::string>{"Canvas", "  Button #go", "    Label #lab"}));
    EXPECT_EQ(vm.rows.get()[0]->twist.get(), "▼");
    EXPECT_EQ(vm.rows.get()[2]->twist.get(), " ");
    EXPECT_FALSE(vm.rows.get()[2]->toggle.can_execute()) << "a leaf has no twist";
    EXPECT_EQ(vm.rows.get()[2]->rowFill.get(), "#00000000");
    EXPECT_EQ(vm.detail.get(), "Nothing selected");

    const std::shared_ptr<editor::InspectorRowViewModel> first = vm.rows.get()[0];
    panel.refresh();
    EXPECT_EQ(panel.view_model()->rows.get()[0], first) << "a row keeps its view-model across refreshes";
    panel.detach();
}

TEST(InspectorPanel, RowCommandSelectsAndTheDetailFollows) {
    Game game;
    editor::InspectorPanel panel;
    panel.attach(game.world);
    panel.refresh();

    panel.view_model()->rows.get()[2]->select.execute();
    EXPECT_TRUE(engine::ui::inspector_selection(game.world).active);
    panel.refresh();

    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_NE(vm.detail.get().find("Label #lab"), std::string::npos) << vm.detail.get();
    EXPECT_EQ(vm.rows.get()[2]->rowFill.get(), "#2f5d3a");
    EXPECT_EQ(vm.rows.get()[1]->rowFill.get(), "#00000000");
    ASSERT_EQ(vm.rules.get().size(), 2u);
    EXPECT_NE(vm.rules.get()[1]->line.get().find("#go > Label"), std::string::npos);
    EXPECT_NE(vm.rules.get()[1]->line.get().find("winner"), std::string::npos);
    panel.detach();
}

TEST(InspectorPanel, PickInTheGameShowsInThePanel) {
    Game game;
    editor::InspectorPanel panel;
    panel.attach(game.world);
    // What a pick click in the game window does (handle_pointer while pick_pointer is on).
    engine::ui::inspector_select(game.world, engine::kPrimaryWindow,
            engine::ui::InspectorPick{.canvas = game.entity, .path = {0}});
    panel.refresh();

    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_NE(vm.detail.get().find("Button #go"), std::string::npos);
    EXPECT_EQ(vm.rows.get()[1]->rowFill.get(), "#2f5d3a");
    panel.detach();
}

TEST(InspectorPanel, TwistCollapsesAndExpands) {
    Game game;
    editor::InspectorPanel panel;
    panel.attach(game.world);
    panel.refresh();

    panel.view_model()->rows.get()[1]->toggle.execute();
    panel.refresh();
    EXPECT_EQ(labels(*panel.view_model()), (std::vector<std::string>{"Canvas", "  Button #go"}));
    EXPECT_EQ(panel.view_model()->rows.get()[1]->twist.get(), "▶");

    panel.view_model()->rows.get()[1]->toggle.execute();
    panel.refresh();
    EXPECT_EQ(panel.view_model()->rows.get().size(), 3u);
    panel.detach();
}

TEST(InspectorPanel, PickIsTwoWay) {
    Game game;
    editor::InspectorPanel panel;
    panel.attach(game.world);
    panel.refresh();
    editor::InspectorViewModel& vm = *panel.view_model();
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

TEST(InspectorPanel, DetachDropsEverythingAndRowsGoInert) {
    Game game;
    editor::InspectorPanel panel;
    panel.attach(game.world);
    panel.view_model()->pick = true;
    panel.refresh();
    const std::shared_ptr<editor::InspectorRowViewModel> kept = panel.view_model()->rows.get()[2];

    panel.detach();
    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_FALSE(panel.attached());
    EXPECT_FALSE(engine::ui::inspector_attached(game.world));
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_TRUE(vm.rules.get().empty());
    EXPECT_TRUE(vm.detail.get().empty());
    EXPECT_FALSE(vm.pick.get());
    EXPECT_FALSE(game.world.ctx<engine::ui::UiInspector>().pick_pointer);

    kept->select.execute();
    EXPECT_FALSE(engine::ui::inspector_selection(game.world).active) << "a stale row does nothing";
}
