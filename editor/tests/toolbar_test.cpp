#include <gtest/gtest.h>

#include "toolbar.h"

TEST(Toolbar, ExportNeedsAFittingProjectAndNothingRunning) {
    editor::Toolbar toolbar;
    editor::EditorViewModel& vm = *toolbar.view_model();
    EXPECT_FALSE(toolbar.can_export_game());
    EXPECT_FALSE(vm.exportGame.can_execute());

    toolbar.show_project("ttt", true);
    EXPECT_TRUE(toolbar.can_export_game());
    EXPECT_TRUE(vm.exportGame.can_execute());

    // A build shows Cancel on the Play button and takes Export away; so does a running game.
    toolbar.show_state(editor::RunState::Building);
    EXPECT_FALSE(vm.exportGame.can_execute());
    toolbar.show_state(editor::RunState::Playing);
    EXPECT_FALSE(vm.exportGame.can_execute());
    toolbar.show_state(editor::RunState::Idle);
    EXPECT_TRUE(vm.exportGame.can_execute());

    toolbar.show_project("ttt", false);
    EXPECT_FALSE(vm.exportGame.can_execute());
}

TEST(Toolbar, ExportButtonRecordsAnExportRequestOnce) {
    editor::Toolbar toolbar;
    toolbar.show_project("ttt", true);
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::None);
    toolbar.view_model()->exportGame.execute();
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::Export);
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::None);
}

TEST(Toolbar, ExportDoesNothingWhileABuildRuns) {
    editor::Toolbar toolbar;
    toolbar.show_project("ttt", true);
    toolbar.show_state(editor::RunState::Building);
    toolbar.view_model()->exportGame.execute();
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::None);
    // Play's button is Cancel now.
    toolbar.view_model()->togglePlay.execute();
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::Stop);
}

TEST(Toolbar, ExportInTheDialogFollowsTheToolbarsState) {
    editor::Toolbar toolbar;
    editor::ExportDialog& dialog = toolbar.export_dialog();
    editor::EditorViewModel& vm = *toolbar.view_model();
    toolbar.show_project("ttt", true);
    dialog.show("C:/games/ttt", "ttt", "Release");
    EXPECT_TRUE(vm.exportConfirm.can_execute());

    // A running build or game: the dialog's Export waits, as the toolbar's Export button does.
    toolbar.show_state(editor::RunState::Building);
    EXPECT_FALSE(vm.exportConfirm.can_execute());
    toolbar.show_state(editor::RunState::Playing);
    EXPECT_FALSE(vm.exportConfirm.can_execute());
    toolbar.show_state(editor::RunState::Idle);
    EXPECT_TRUE(vm.exportConfirm.can_execute());

    // A project that does not fit the SDK.
    toolbar.show_project("ttt", false);
    EXPECT_FALSE(vm.exportConfirm.can_execute());
}

TEST(Toolbar, TheExportButtonOnlyAsksForTheDialogAndNeverExportsByItself) {
    editor::Toolbar toolbar;
    toolbar.show_project("ttt", true);
    toolbar.view_model()->exportGame.execute();
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::Export);
    EXPECT_FALSE(toolbar.export_dialog().is_open());
    EXPECT_FALSE(toolbar.export_dialog().take_choice().has_value());
}
