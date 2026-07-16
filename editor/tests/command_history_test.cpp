#include <gtest/gtest.h>

#include "command_history.h"
#include "editor_selection.h"
#include "explorer_panel.h"
#include "inspector_panel.h"

#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

class TempProject {
public:
    TempProject()
        : root_(std::filesystem::temp_directory_path() / "wind_command_history_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        std::ofstream(root_ / "wind_project.toml", std::ios::binary) << "";
    }

    ~TempProject() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

}

TEST(CommandHistory, FileAndEntityStepsUndoInReverse) {
    TempProject project;
    editor::CommandHistory history;
    editor::EditorSelection selection;
    editor::ExplorerPanel explorer{selection, history};
    explorer.open(project.root());
    explorer.new_folder();
    EXPECT_TRUE(std::filesystem::is_directory(project.root() / "New Folder"));

    engine::ecs::World world;
    const engine::ecs::Entity hero = world.create();
    world.emplace<engine::ecs::EntityName>(hero, engine::ecs::EntityName{.id = "hero"});
    world.emplace<engine::Transform>(hero, engine::Transform{.position = {1.0f, 0.0f, 0.0f}});
    world.ctx<engine::ecs::WorldSerializer>();

    editor::InspectorPanel inspector{selection};
    inspector.set_edit_world(&world);
    inspector.set_journal(&history, [] {});
    selection.select(editor::EntitySelection{.entity = hero});
    inspector.refresh();
    inspector.edit_text("transform", "position", 0, "8");
    EXPECT_FLOAT_EQ(world.get<engine::Transform>(hero).position.x, 8.0f);

    EXPECT_TRUE(history.undo());
    EXPECT_FLOAT_EQ(world.get<engine::Transform>(hero).position.x, 1.0f);
    EXPECT_TRUE(std::filesystem::is_directory(project.root() / "New Folder"));

    EXPECT_TRUE(history.undo());
    EXPECT_FALSE(std::filesystem::exists(project.root() / "New Folder"));

    EXPECT_TRUE(history.redo());
    EXPECT_TRUE(std::filesystem::is_directory(project.root() / "New Folder"));
    EXPECT_TRUE(history.redo());
    EXPECT_FLOAT_EQ(world.get<engine::Transform>(hero).position.x, 8.0f);
}
