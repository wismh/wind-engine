#include <gtest/gtest.h>

#include "core/file_dialog_state.h"
#include "fixtures/fake_services.h"

#include <engine/core/file_dialog.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace {

// What the SDL window control does: one state shared by the call and the dialog's callback.
struct Shown {
    std::shared_ptr<engine::FileDialogState> state = std::make_shared<engine::FileDialogState>();
    engine::FileDialogCall call{state};
};

}

TEST(FileDialog, AnswerFromAnotherThreadIsVisibleOnlyAfterDeliver) {
    engine::FileDialogCompletions completions;
    Shown shown;
    std::thread dialog_thread([&completions, state = shown.state] {
        completions.push(state, engine::FileDialogResult{.path = std::filesystem::path("C:/games/game.dll")});
    });
    dialog_thread.join();

    EXPECT_TRUE(shown.call.pending()) << "nothing is visible before deliver";
    EXPECT_FALSE(shown.call.take().has_value());

    completions.deliver();
    EXPECT_FALSE(shown.call.pending());
    const std::optional<engine::FileDialogResult> answer = shown.call.take();
    ASSERT_TRUE(answer.has_value());
    ASSERT_TRUE(answer->path.has_value());
    EXPECT_EQ(*answer->path, std::filesystem::path("C:/games/game.dll"));

    EXPECT_FALSE(shown.call.take().has_value()) << "an answer is taken once";
    EXPECT_FALSE(shown.call.pending());
}

TEST(FileDialog, UserCancelIsAnAnswerWithoutAPath) {
    engine::FileDialogCompletions completions;
    Shown shown;
    completions.push(shown.state, engine::FileDialogResult{});
    completions.deliver();

    const std::optional<engine::FileDialogResult> answer = shown.call.take();
    ASSERT_TRUE(answer.has_value());
    EXPECT_FALSE(answer->path.has_value());
}

TEST(FileDialog, EachCallGetsItsOwnAnswer) {
    engine::FileDialogCompletions completions;
    Shown a;
    Shown b;
    completions.push(b.state, engine::FileDialogResult{.path = std::filesystem::path("b.dll")});
    completions.push(a.state, engine::FileDialogResult{.path = std::filesystem::path("a.dll")});
    completions.deliver();

    EXPECT_EQ(a.call.take()->path, std::filesystem::path("a.dll"));
    EXPECT_EQ(b.call.take()->path, std::filesystem::path("b.dll"));
}

TEST(FileDialog, CancelDropsTheAnswer) {
    engine::FileDialogCompletions completions;
    Shown shown;
    shown.call.cancel();
    EXPECT_FALSE(shown.call.pending());

    completions.push(shown.state, engine::FileDialogResult{.path = std::filesystem::path("x.dll")});
    completions.deliver();
    EXPECT_FALSE(shown.state->result.has_value());
    EXPECT_FALSE(shown.call.take().has_value());
}

TEST(FileDialog, DestroyedCallDropsTheAnswer) {
    engine::FileDialogCompletions completions;
    auto state = std::make_shared<engine::FileDialogState>();
    {
        const engine::FileDialogCall call{state};
        EXPECT_TRUE(call.pending());
    }
    EXPECT_TRUE(state->cancelled());

    completions.push(state, engine::FileDialogResult{.path = std::filesystem::path("x.dll")});
    completions.deliver();
    EXPECT_FALSE(state->result.has_value());
}

TEST(FileDialog, MoveAssignCancelsTheCallItReplaces) {
    Shown first;
    Shown second;
    first.call = std::move(second.call);

    EXPECT_TRUE(first.state->cancelled());
    EXPECT_FALSE(second.state->cancelled());
    EXPECT_TRUE(first.call.pending());
    EXPECT_FALSE(second.call.pending());
}

TEST(FileDialog, ResolvedCallHoldsItsAnswerAndEmptyCallHoldsNothing) {
    engine::FileDialogCall resolved =
            engine::FileDialogCall::resolved(engine::FileDialogResult{.path = std::filesystem::path("r.dll")});
    EXPECT_FALSE(resolved.pending());
    EXPECT_EQ(resolved.take()->path, std::filesystem::path("r.dll"));

    engine::FileDialogCall empty;
    EXPECT_FALSE(empty.pending());
    EXPECT_FALSE(empty.take().has_value());
    empty.cancel();
}

TEST(FileDialog, AFolderAnswerTravelsTheSameWayAsAFilesAndEmptyMeansCancelled) {
    engine::FileDialogCompletions completions;
    Shown chosen;
    Shown cancelled;
    completions.push(chosen.state, engine::FileDialogResult{.path = std::filesystem::path("C:/games/out")});
    completions.push(cancelled.state, engine::FileDialogResult{});
    completions.deliver();
    const std::optional<engine::FileDialogResult> folder = chosen.call.take();
    ASSERT_TRUE(folder.has_value());
    EXPECT_EQ(folder->path, std::filesystem::path("C:/games/out"));
    const std::optional<engine::FileDialogResult> none = cancelled.call.take();
    ASSERT_TRUE(none.has_value());
    EXPECT_FALSE(none->path.has_value());
}

TEST(FileDialog, TheFakeWindowControlAnswersAFolderRequestAtOnceAndRemembersWhereItOpened) {
    fakes::FakeWindowControl windows;
    windows.folder_answer = std::filesystem::path("C:/out");
    engine::FileDialogCall first = windows.request_open_folder(engine::kPrimaryWindow, "C:/start");
    EXPECT_FALSE(first.pending());
    EXPECT_EQ(first.take()->path, std::filesystem::path("C:/out"));

    windows.folder_answer.reset();
    engine::FileDialogCall second = windows.request_open_folder(engine::kPrimaryWindow, {});
    ASSERT_TRUE(second.take().has_value());
    ASSERT_EQ(windows.folder_starts.size(), 2u);
    EXPECT_EQ(windows.folder_starts[0], std::filesystem::path("C:/start"));
    EXPECT_TRUE(windows.folder_starts[1].empty());
}
