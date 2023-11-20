#include <gtest/gtest.h>

#include "export_dialog.h"
#include "toolbar.h"

TEST(Toolbar, ExportNeedsAFittingProjectAndNothingRunning) {
    editor::Toolbar toolbar;
    editor::EditorViewModel& vm = *toolbar.view_model();
    EXPECT_FALSE(toolbar.can_export_game());
    EXPECT_FALSE(vm.exportGame.can_execute());

    toolbar.show_project("ttt", true);
    EXPECT_TRUE(toolbar.can_export_game());
    EXPECT_TRUE(vm.exportGame.can_execute());

    // A build shows Stop on the Play button and takes Export away; so does a running game.
    toolbar.show_state(editor::RunState::Building);
    EXPECT_FALSE(vm.exportGame.can_execute());
    toolbar.show_state(editor::RunState::Playing);
    EXPECT_FALSE(vm.exportGame.can_execute());
    toolbar.show_state(editor::RunState::Idle);
    EXPECT_TRUE(vm.exportGame.can_execute());

    toolbar.show_project("ttt", false);
    EXPECT_FALSE(vm.exportGame.can_execute());
}

TEST(Toolbar, ExportRecordsAnExportRequestOnce) {
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
    // Play's button is Stop now.
    toolbar.view_model()->togglePlay.execute();
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::Stop);
}

TEST(Toolbar, ExportInTheDialogFollowsTheToolbarsState) {
    editor::Toolbar toolbar;
    editor::ExportDialog dialog;
    toolbar.show_project("ttt", true);
    dialog.set_available(toolbar.can_export_game());
    dialog.show("C:/games/ttt", "ttt", "Release");
    EXPECT_TRUE(dialog.can_confirm());

    // A running build or game: the dialog's Export waits, as the toolbar's Export command does.
    toolbar.show_state(editor::RunState::Building);
    dialog.set_available(toolbar.can_export_game());
    EXPECT_FALSE(dialog.can_confirm());
    toolbar.show_state(editor::RunState::Playing);
    dialog.set_available(toolbar.can_export_game());
    EXPECT_FALSE(dialog.can_confirm());
    toolbar.show_state(editor::RunState::Idle);
    dialog.set_available(toolbar.can_export_game());
    EXPECT_TRUE(dialog.can_confirm());

    // A project that does not fit the SDK.
    toolbar.show_project("ttt", false);
    dialog.set_available(toolbar.can_export_game());
    EXPECT_FALSE(dialog.can_confirm());
}

TEST(Toolbar, PlayButtonShowsPlayAndStopIcons) {
    editor::Toolbar toolbar;
    EXPECT_EQ(toolbar.view_model()->playLabel.get(), "\xe2\x96\xb6 Play");

    toolbar.show_state(editor::RunState::Building);
    EXPECT_EQ(toolbar.view_model()->playLabel.get(), "\xe2\x96\xa0 Stop");

    toolbar.show_state(editor::RunState::Playing);
    EXPECT_EQ(toolbar.view_model()->playLabel.get(), "\xe2\x96\xa0 Stop");

    toolbar.show_state(editor::RunState::Idle);
    EXPECT_EQ(toolbar.view_model()->playLabel.get(), "\xe2\x96\xb6 Play");
}
