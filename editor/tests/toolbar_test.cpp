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
