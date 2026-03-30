#include <gtest/gtest.h>

#include "core/file_dialog_queue.h"

#include <engine/core/file_dialog.h>
#include <engine/core/worlds.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/resources/fatal_error.h>

#include <filesystem>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

namespace {

class QuietFatal final : public engine::IFatalError {
public:
    void report(std::string_view) override {}
};

std::vector<engine::FileDialogResultEvent> read_results(engine::ecs::World& world) {
    std::vector<engine::FileDialogResultEvent> out;
    for (const engine::FileDialogResultEvent& event : engine::ecs::EventReader<engine::FileDialogResultEvent>{world}) {
        out.push_back(event);
    }
    return out;
}

}

TEST(FileDialog, AnswerFromAnotherThreadArrivesInTheOwnerWorldOnDeliver) {
    QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& editor = worlds.add();
    engine::ecs::World& game = worlds.add();
    const engine::WindowId editor_window{3};
    worlds.bind_window(editor_window, editor);
    worlds.bind_window(engine::kPrimaryWindow, game);

    engine::FileDialogQueue queue;
    const engine::FileDialogRequest request = queue.begin(editor_window);
    std::thread dialog_thread(
            [&queue, request] { queue.complete(request, std::filesystem::path("C:/games/game.dll")); });
    dialog_thread.join();

    EXPECT_TRUE(read_results(editor).empty()) << "nothing is sent before deliver";
    queue.deliver(worlds);

    const std::vector<engine::FileDialogResultEvent> results = read_results(editor);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].request, request);
    ASSERT_TRUE(results[0].path.has_value());
    EXPECT_EQ(*results[0].path, std::filesystem::path("C:/games/game.dll"));
    EXPECT_TRUE(read_results(game).empty());

    queue.deliver(worlds);
    EXPECT_EQ(read_results(editor).size(), 1u) << "an answer is delivered once";
}

TEST(FileDialog, CancelIsAnEmptyPath) {
    QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& editor = worlds.add();
    const engine::WindowId editor_window{3};
    worlds.bind_window(editor_window, editor);

    engine::FileDialogQueue queue;
    const engine::FileDialogRequest request = queue.begin(editor_window);
    queue.complete(request, std::nullopt);
    queue.deliver(worlds);

    const std::vector<engine::FileDialogResultEvent> results = read_results(editor);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].path.has_value());
}

TEST(FileDialog, RequestsGetDistinctIdsAndKeepTheirOwnOwner) {
    QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& first = worlds.add();
    engine::ecs::World& second = worlds.add();
    const engine::WindowId first_window{1};
    const engine::WindowId second_window{2};
    worlds.bind_window(first_window, first);
    worlds.bind_window(second_window, second);

    engine::FileDialogQueue queue;
    const engine::FileDialogRequest a = queue.begin(first_window);
    const engine::FileDialogRequest b = queue.begin(second_window);
    EXPECT_NE(a, b);
    queue.complete(b, std::filesystem::path("b.dll"));
    queue.complete(a, std::filesystem::path("a.dll"));
    queue.deliver(worlds);

    const auto first_results = read_results(first);
    const auto second_results = read_results(second);
    ASSERT_EQ(first_results.size(), 1u);
    ASSERT_EQ(second_results.size(), 1u);
    EXPECT_EQ(first_results[0].request, a);
    EXPECT_EQ(second_results[0].request, b);
}

TEST(FileDialog, AnswerForAWindowWithoutAWorldIsDropped) {
    QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& other = worlds.add();
    worlds.bind_window(engine::kPrimaryWindow, other);

    engine::FileDialogQueue queue;
    const engine::FileDialogRequest request = queue.begin(engine::WindowId{9});
    queue.complete(request, std::filesystem::path("x.dll"));
    queue.deliver(worlds);

    EXPECT_TRUE(read_results(other).empty());
}
