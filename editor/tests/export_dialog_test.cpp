#include <gtest/gtest.h>

#include "export_dialog.h"
#include "project_export.h"

#include "fixtures/fake_services.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string text_of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// A project directory under the temp directory, removed with the object.
class Dirs {
public:
    Dirs()
        : root_(fs::temp_directory_path() / "wind_export_dialog_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        fs::remove_all(root_);
        fs::create_directories(project());
    }
    ~Dirs() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] fs::path project() const {
        return root_ / "game";
    }
    [[nodiscard]] fs::path elsewhere() const {
        return root_ / "elsewhere";
    }

private:
    fs::path root_;
};

// The dialog over its view-model, open for the project of `dirs` against a Release SDK.
struct Fixture {
    explicit Fixture(const Dirs& where, const std::string& sdk_config = "Release")
        : vm(*dialog.view_model())
        , dirs(where) {
        dialog.set_available(true);
        dialog.show(dirs.project(), "my_game", sdk_config);
    }

    // The frame the editor runs after the UI pass.
    void frame() {
        dialog.frame(windows, engine::kPrimaryWindow);
    }
    void type(const fs::path& folder) {
        vm.path = text_of(folder);
        frame();
    }

    editor::ExportDialog dialog;
    editor::ExportViewModel& vm;
    fakes::FakeWindowControl windows;
    const Dirs& dirs;
};

}

TEST(ExportDialog, ShowsWithTheDefaultFolderAndTheReleaseBuild) {
    const Dirs dirs;
    Fixture f(dirs);
    EXPECT_TRUE(f.dialog.is_open());
    EXPECT_EQ(f.vm.path.get(), text_of(editor::default_export_directory(dirs.project(), "my_game")));
    EXPECT_FALSE(f.vm.development.get());
    EXPECT_FALSE(f.dialog.has_problem());
    EXPECT_TRUE(f.dialog.message().empty());
    EXPECT_TRUE(f.dialog.can_confirm());
    EXPECT_TRUE(f.vm.confirm.can_execute());
}

TEST(ExportDialog, TheLastFolderAndFlavourOfTheSessionComeBackForTheSameProject) {
    const Dirs dirs;
    Fixture f(dirs);
    f.vm.development = true;
    f.type(dirs.elsewhere() / "build_one");
    f.dialog.close();
    EXPECT_FALSE(f.dialog.is_open());

    f.dialog.show(dirs.project(), "my_game", "Release");
    EXPECT_EQ(f.vm.path.get(), text_of(dirs.elsewhere() / "build_one"));
    EXPECT_TRUE(f.vm.development.get());

    // Another project starts from its own default.
    f.dialog.show(dirs.elsewhere() / "other", "other_game", "Release");
    EXPECT_EQ(f.vm.path.get(),
            text_of(editor::default_export_directory(dirs.elsewhere() / "other", "other_game")));
    EXPECT_FALSE(f.vm.development.get());
}

TEST(ExportDialog, AnEmptyFolderAndOneThatHoldsTheProjectAreProblems) {
    const Dirs dirs;
    Fixture f(dirs);
    f.vm.path = std::string("   ");
    f.frame();
    EXPECT_TRUE(f.dialog.has_problem());
    EXPECT_NE(f.dialog.message().find("Choose the folder"), std::string::npos);
    EXPECT_FALSE(f.dialog.can_confirm());
    EXPECT_FALSE(f.vm.confirm.can_execute());
    EXPECT_EQ(f.vm.messageTone.get(), "#ff6b68");

    f.type(dirs.project());
    EXPECT_TRUE(f.dialog.has_problem());
    EXPECT_NE(f.dialog.message().find("holds the project"), std::string::npos);
    EXPECT_FALSE(f.dialog.can_confirm());

    f.type(dirs.elsewhere() / "out");
    EXPECT_FALSE(f.dialog.has_problem());
    EXPECT_TRUE(f.dialog.message().empty());
    EXPECT_TRUE(f.dialog.can_confirm());
}

TEST(ExportDialog, ANonEmptyFolderThatNoExportMadeIsRefusedButAnEarlierExportIsOnlyAWarning) {
    const Dirs dirs;
    Fixture f(dirs);
    const fs::path mine = dirs.elsewhere() / "mine";
    fs::create_directories(mine);
    std::ofstream(mine / "notes.txt") << "somebody's files";
    f.type(mine);
    EXPECT_TRUE(f.dialog.has_problem());
    EXPECT_NE(f.dialog.message().find("was not made by an Export"), std::string::npos);
    EXPECT_FALSE(f.dialog.can_confirm());

    const fs::path earlier = dirs.elsewhere() / "earlier";
    fs::create_directories(earlier);
    std::ofstream(earlier / editor::kExportMarker) << "marker";
    std::ofstream(earlier / "game.exe") << "exe";
    f.type(earlier);
    EXPECT_FALSE(f.dialog.has_problem());
    EXPECT_NE(f.dialog.message().find("earlier export"), std::string::npos);
    EXPECT_EQ(f.vm.messageTone.get(), "#e5b455");
    EXPECT_TRUE(f.dialog.can_confirm());
}

TEST(ExportDialog, ConfirmRecordsTheChoiceOnceAndClosesTheDialog) {
    const Dirs dirs;
    Fixture f(dirs);
    f.vm.development = true;
    f.type(dirs.elsewhere() / "out");
    EXPECT_FALSE(f.dialog.take_choice().has_value());

    f.vm.confirm.execute();
    EXPECT_FALSE(f.dialog.is_open());
    EXPECT_TRUE(f.dialog.take_close_requested());
    const std::optional<editor::ExportChoice> choice = f.dialog.take_choice();
    ASSERT_TRUE(choice.has_value());
    EXPECT_EQ(choice->directory, dirs.elsewhere() / "out");
    EXPECT_TRUE(choice->development);
    EXPECT_FALSE(f.dialog.take_choice().has_value());
}

TEST(ExportDialog, ARelativeFolderIsTakenFromTheProject) {
    const Dirs dirs;
    Fixture f(dirs);
    f.vm.path = std::string("dist/win");
    f.frame();
    f.vm.confirm.execute();
    const std::optional<editor::ExportChoice> choice = f.dialog.take_choice();
    ASSERT_TRUE(choice.has_value());
    EXPECT_EQ(choice->directory, dirs.project() / "dist" / "win");
}

TEST(ExportDialog, TextTypedSinceTheLastFrameIsCheckedWhenExportIsPressed) {
    const Dirs dirs;
    Fixture f(dirs);
    // Typed and confirmed in one frame (Enter in the field): the project itself is no target.
    f.vm.path = text_of(dirs.project());
    f.dialog.confirm();
    EXPECT_TRUE(f.dialog.is_open());
    EXPECT_FALSE(f.dialog.take_choice().has_value());
    EXPECT_TRUE(f.dialog.has_problem());
}

TEST(ExportDialog, ExportWaitsForAnEditorThatBuildsNothingElse) {
    const Dirs dirs;
    Fixture f(dirs);
    f.dialog.set_available(false);
    EXPECT_FALSE(f.vm.confirm.can_execute());
    f.vm.confirm.execute();
    EXPECT_TRUE(f.dialog.is_open());
    EXPECT_FALSE(f.dialog.take_choice().has_value());

    f.dialog.set_available(true);
    EXPECT_TRUE(f.vm.confirm.can_execute());
}

TEST(ExportDialog, CancelAndDismissLeaveNoChoice) {
    const Dirs dirs;
    Fixture f(dirs);
    f.vm.cancel.execute();
    EXPECT_FALSE(f.dialog.is_open());
    EXPECT_TRUE(f.dialog.take_close_requested());
    EXPECT_FALSE(f.dialog.take_choice().has_value());

    f.dialog.show(dirs.project(), "my_game", "Release");
    f.dialog.close();
    EXPECT_FALSE(f.dialog.is_open());
    EXPECT_FALSE(f.vm.confirm.can_execute());
}

TEST(ExportDialog, BrowseAsksTheNearestExistingFolderAndTakesTheAnswer) {
    const Dirs dirs;
    Fixture f(dirs);
    const fs::path picked = dirs.elsewhere() / "picked";
    f.windows.folder_answer = picked;
    f.vm.browse.execute();
    // Nothing is asked inside the UI pass.
    EXPECT_TRUE(f.windows.folder_starts.empty());
    f.frame();
    // <project>/export/my_game does not exist, <project> does.
    ASSERT_EQ(f.windows.folder_starts.size(), 1u);
    EXPECT_EQ(f.windows.folder_starts[0], dirs.project());
    EXPECT_EQ(f.vm.path.get(), text_of(picked));
    // The answer is checked like typed text.
    EXPECT_FALSE(f.dialog.has_problem());

    // A cancelled dialog leaves the text; one request per press.
    f.windows.folder_answer.reset();
    f.vm.browse.execute();
    f.frame();
    f.frame();
    EXPECT_EQ(f.windows.folder_starts.size(), 2u);
    EXPECT_EQ(f.vm.path.get(), text_of(picked));
}

TEST(ExportDialog, ADebugSdkHasNoDevelopmentBuild) {
    const Dirs dirs;
    Fixture f(dirs, "Debug");
    EXPECT_TRUE(f.dialog.can_confirm());
    f.vm.development = true;
    f.frame();
    EXPECT_TRUE(f.dialog.has_problem());
    EXPECT_NE(f.dialog.message().find("Release SDK"), std::string::npos);
    EXPECT_FALSE(f.dialog.can_confirm());

    f.vm.development = false;
    f.frame();
    EXPECT_FALSE(f.dialog.has_problem());
    EXPECT_TRUE(f.dialog.can_confirm());
}
