#include <gtest/gtest.h>

#include "editor_selection.h"
#include "inspector_panel.h"

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
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
        engine::ui::set_inspector_attached(world, true);
    }

    engine::ecs::World world;
    engine::ecs::Entity entity{};
};

// One text file in a directory of its own.
class TextFile {
public:
    explicit TextFile(std::string_view text)
        : dir_(std::filesystem::temp_directory_path() / "wind_inspector_panel_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        write(text);
    }

    ~TextFile() {
        std::error_code error;
        std::filesystem::remove_all(dir_, error);
    }

    TextFile(const TextFile&) = delete;
    TextFile& operator=(const TextFile&) = delete;

    void write(std::string_view text) const { std::ofstream(path(), std::ios::binary) << text; }

    [[nodiscard]] std::filesystem::path path() const { return dir_ / "notes.txt"; }

    [[nodiscard]] editor::AssetSelection selection() const {
        return editor::AssetSelection{.key = "notes.txt", .path = path(), .size = std::filesystem::file_size(path())};
    }

private:
    std::filesystem::path dir_;
};

std::vector<std::string> headings(const editor::InspectorViewModel& vm) {
    std::vector<std::string> out;
    for (const auto& section : vm.sections.get()) {
        out.push_back(section->heading.get());
    }
    return out;
}

std::vector<std::string> lines(const editor::InspectorSectionViewModel& section) {
    std::vector<std::string> out;
    for (const auto& line : section.lines.get()) {
        out.push_back(line->text.get());
    }
    return out;
}

}

TEST(InspectorPanel, NothingSelectedSaysHowToSelect) {
    const editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    panel.refresh();
    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.title.get(), "Nothing selected");
    EXPECT_NE(vm.subtitle.get().find("UI Tree"), std::string::npos);
    EXPECT_TRUE(vm.sections.get().empty());
}

TEST(InspectorPanel, AFileIsReadWhenSelectedAndAgainWhenSelectedAgain) {
    const TextFile file("first\nsecond\n");
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    selection.select(file.selection());
    panel.refresh();
    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.title.get(), "notes.txt");
    EXPECT_EQ(vm.subtitle.get(), "File");
    EXPECT_EQ(headings(vm), (std::vector<std::string>{"File", "Content"}));
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"first", "second"}));

    file.write("changed\n");
    panel.refresh();
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"first", "second"}))
            << "the disk is not read every frame";
    selection.select(file.selection());
    panel.refresh();
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"changed"}));
}

TEST(InspectorPanel, ACollapsedSectionStaysCollapsedForTheNextSelection) {
    const TextFile file("first\n");
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    selection.select(file.selection());
    panel.refresh();
    const editor::InspectorViewModel& vm = *panel.view_model();
    const std::shared_ptr<editor::InspectorSectionViewModel> content = vm.sections.get()[1];
    EXPECT_TRUE(content->expanded.get());

    content->toggle.execute();
    EXPECT_FALSE(vm.sections.get()[1]->expanded.get());
    EXPECT_TRUE(vm.sections.get()[1]->lines.get().empty());
    EXPECT_EQ(lines(*vm.sections.get()[0]).size(), 2u) << "only that section";

    selection.select(file.selection());
    panel.refresh();
    EXPECT_FALSE(vm.sections.get()[1]->expanded.get());
    EXPECT_EQ(vm.sections.get()[1], content) << "a section keeps its view-model";

    content->toggle.execute();
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"first"}));
}

TEST(InspectorPanel, AUiElementShowsItsComputedStyleAndRules) {
    Game game;
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    panel.attach(game.world);
    engine::ui::inspector_select(game.world, engine::kPrimaryWindow,
            engine::ui::InspectorPick{.canvas = game.entity, .path = {0, 0}});
    selection.select(editor::UiElementSelection{});
    panel.refresh();

    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.title.get(), "Label #lab");
    EXPECT_EQ(vm.subtitle.get(), "UI element");
    EXPECT_EQ(headings(vm), (std::vector<std::string>{"Computed", "Rules"}));
    const std::vector<std::string> computed = lines(*vm.sections.get()[0]);
    EXPECT_NE(std::ranges::find(computed, "pseudo: (none)"), computed.end());
    const std::vector<std::string> rules = lines(*vm.sections.get()[1]);
    ASSERT_GE(rules.size(), 4u);
    EXPECT_EQ(rules.front().find("Label"), 0u) << rules.front();
    bool winner = false;
    for (const std::string& line : rules) {
        winner = winner || (line.find("#go > Label") != std::string::npos && line.find("winner") != std::string::npos);
    }
    EXPECT_TRUE(winner);

    // The probe moves to another element: the next refresh follows it without a new editor selection.
    engine::ui::inspector_select(game.world, engine::kPrimaryWindow,
            engine::ui::InspectorPick{.canvas = game.entity, .path = {0}});
    panel.refresh();
    EXPECT_EQ(vm.title.get(), "Button #go");
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"No rule matches."}));

    panel.detach();
    EXPECT_FALSE(panel.attached());
    EXPECT_EQ(vm.title.get(), "Nothing selected");
    EXPECT_TRUE(vm.sections.get().empty());
}

TEST(InspectorPanel, AUiElementWithoutTheGameShowsNothing) {
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    selection.select(editor::UiElementSelection{});
    panel.refresh();
    EXPECT_EQ(panel.view_model()->title.get(), "Nothing selected");
}

TEST(InspectorPanel, DetachKeepsAFile) {
    const TextFile file("first\n");
    Game game;
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    panel.attach(game.world);
    selection.select(file.selection());
    panel.refresh();
    panel.detach();
    EXPECT_EQ(panel.view_model()->title.get(), "notes.txt");
    EXPECT_EQ(headings(*panel.view_model()), (std::vector<std::string>{"File", "Content"}));
}
