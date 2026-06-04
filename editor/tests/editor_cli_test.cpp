#include <gtest/gtest.h>

#include "editor_cli.h"
#include "toolbar.h"

#include <filesystem>
#include <string>
#include <variant>

namespace {

const engine::CliValue* field(const engine::CliReply& reply, const std::string& name) {
    for (const auto& [key, value] : reply.result) {
        if (key == name) {
            return &value;
        }
    }
    return nullptr;
}

std::string text(const engine::CliReply& reply, const std::string& name) {
    const engine::CliValue* value = field(reply, name);
    if (value == nullptr || !std::holds_alternative<std::string>(*value)) {
        return "<missing>";
    }
    return std::get<std::string>(*value);
}

std::filesystem::path absolute_dir() {
    return std::filesystem::temp_directory_path() / "wind_editor_cli_test";
}

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

}

TEST(EditorCli, StateReportsTheToolbarAndTheFacts) {
    editor::Toolbar toolbar;
    editor::EditorCli cli{toolbar};
    toolbar.show_status("Open a project.");

    const auto idle = cli.handle(engine::CliCommand{"state", {}}, editor::EditorFacts{});
    ASSERT_TRUE(idle.has_value());
    EXPECT_TRUE(idle->ok);
    EXPECT_EQ(text(*idle, "run"), "idle");
    EXPECT_EQ(text(*idle, "status"), "Open a project.");
    ASSERT_NE(field(*idle, "playable"), nullptr);
    EXPECT_EQ(std::get<bool>(*field(*idle, "playable")), false);
    ASSERT_NE(field(*idle, "project"), nullptr);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(*field(*idle, "project")));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(*field(*idle, "sdk")));

    toolbar.show_project("ttt", true);
    toolbar.show_state(editor::RunState::Building);
    const auto building = cli.handle(engine::CliCommand{"state", {}},
            editor::EditorFacts{.project = "ttt", .project_dir = absolute_dir(), .sdk = "0.2.0"});
    ASSERT_TRUE(building.has_value());
    EXPECT_EQ(text(*building, "run"), "building");
    EXPECT_EQ(text(*building, "project"), "ttt");
    EXPECT_EQ(text(*building, "project_dir"), utf8(absolute_dir()));
    EXPECT_EQ(text(*building, "sdk"), "0.2.0");

    toolbar.show_state(editor::RunState::Playing);
    EXPECT_EQ(text(*cli.handle(engine::CliCommand{"state", {}}, editor::EditorFacts{}), "run"), "playing");
}

TEST(EditorCli, PlayRecordsTheToolbarRequestOnlyWhenPlayable) {
    editor::Toolbar toolbar;
    editor::EditorCli cli{toolbar};
    toolbar.show_status("The project needs engine 0.2.0; this editor is 0.1.0.");

    const auto refused = cli.handle(engine::CliCommand{"play", {}}, editor::EditorFacts{});
    ASSERT_TRUE(refused.has_value());
    EXPECT_FALSE(refused->ok);
    EXPECT_EQ(refused->error, "not playable: The project needs engine 0.2.0; this editor is 0.1.0.");
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::None);

    toolbar.show_project("ttt", true);
    const auto accepted = cli.handle(engine::CliCommand{"play", {}}, editor::EditorFacts{});
    ASSERT_TRUE(accepted.has_value());
    EXPECT_TRUE(accepted->ok);
    EXPECT_EQ(text(*accepted, "requested"), "play");
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::Play);

    toolbar.show_state(editor::RunState::Playing);
    const auto again = cli.handle(engine::CliCommand{"play", {}}, editor::EditorFacts{});
    ASSERT_TRUE(again.has_value());
    EXPECT_FALSE(again->ok);
    EXPECT_EQ(again->error, "already playing");
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::None);
}

TEST(EditorCli, StopCancelsABuildOrStopsAGame) {
    editor::Toolbar toolbar;
    editor::EditorCli cli{toolbar};

    const auto idle = cli.handle(engine::CliCommand{"stop", {}}, editor::EditorFacts{});
    ASSERT_TRUE(idle.has_value());
    EXPECT_FALSE(idle->ok);
    EXPECT_EQ(idle->error, "not playing");
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::None);

    toolbar.show_state(editor::RunState::Building);
    const auto building = cli.handle(engine::CliCommand{"stop", {}}, editor::EditorFacts{});
    ASSERT_TRUE(building.has_value());
    EXPECT_TRUE(building->ok);
    EXPECT_EQ(text(*building, "was"), "building");
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::Stop);

    toolbar.show_state(editor::RunState::Playing);
    const auto playing = cli.handle(engine::CliCommand{"stop", {}}, editor::EditorFacts{});
    ASSERT_TRUE(playing.has_value());
    EXPECT_EQ(text(*playing, "was"), "playing");
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::Stop);
}

TEST(EditorCli, OpenTakesAnAbsoluteDirectoryOrItsProjectFile) {
    editor::Toolbar toolbar;
    editor::EditorCli cli{toolbar};

    const auto relative = cli.handle(engine::CliCommand{"open", "games/ttt"}, editor::EditorFacts{});
    ASSERT_TRUE(relative.has_value());
    EXPECT_FALSE(relative->ok);
    EXPECT_FALSE(cli.take_open().has_value());

    const auto empty = cli.handle(engine::CliCommand{"open", {}}, editor::EditorFacts{});
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty->error, "open needs a path");

    const auto dialog = cli.handle(engine::CliCommand{"open", utf8(absolute_dir())},
            editor::EditorFacts{.dialog_open = true});
    ASSERT_TRUE(dialog.has_value());
    EXPECT_FALSE(dialog->ok);
    EXPECT_FALSE(cli.take_open().has_value());

    const auto file = cli.handle(engine::CliCommand{"open", utf8(absolute_dir() / "wind_project.toml")},
            editor::EditorFacts{});
    ASSERT_TRUE(file.has_value());
    EXPECT_TRUE(file->ok);
    const std::optional<std::filesystem::path> taken = cli.take_open();
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, absolute_dir());
    EXPECT_FALSE(cli.take_open().has_value());

    toolbar.show_state(editor::RunState::Playing);
    const auto playing = cli.handle(engine::CliCommand{"open", utf8(absolute_dir())}, editor::EditorFacts{});
    ASSERT_TRUE(playing.has_value());
    EXPECT_EQ(playing->error, "stop first: the editor is playing");
    EXPECT_FALSE(cli.take_open().has_value());
}

TEST(EditorCli, OtherCommandsAreNotTheEditors) {
    editor::Toolbar toolbar;
    editor::EditorCli cli{toolbar};
    EXPECT_FALSE(cli.handle(engine::CliCommand{"launch", {}}, editor::EditorFacts{}).has_value());
}
