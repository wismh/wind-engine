#include <gtest/gtest.h>

#include "play_session.h"

#include "fixtures/fake_services.h"
#include "fixtures/game_module/fixture_log.h"

#include <engine/core/input_system.h>
#include <engine/core/window_desc.h>
#include <engine/igame.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kFixture{WIND_FIXTURE_GAME};
const std::filesystem::path kWrongBuildId{WIND_FIXTURE_GAME_WRONG_BUILD_ID};

// The window half of Play and Stop, the way EngineHost does it minus the real window: attach binds
// kPrimaryWindow to the game world, detach unbinds it. Every call is a line in `log`.
class RecordingHost final : public editor::IPlayHost {
public:
    RecordingHost(engine::Worlds& worlds, std::vector<std::string>& log) : worlds_(&worlds), log_(&log) {}

    std::expected<void, std::string> load_catalog(const std::filesystem::path& assets_dir) override {
        log_->push_back("host.load");
        loaded = assets_dir;
        if (fail_load) {
            return std::unexpected(std::string("Game catalog: invalid TOML"));
        }
        return {};
    }
    void unload_catalog(const std::filesystem::path& assets_dir) override {
        log_->push_back("host.unload");
        unloaded = assets_dir;
    }
    void attach(engine::IGame& game) override {
        log_->push_back("host.attach");
        worlds_->bind_window(engine::kPrimaryWindow, game.world());
    }
    void detach() override {
        log_->push_back("host.detach");
        worlds_->unbind_window(engine::kPrimaryWindow);
    }
    void attach_tools(engine::ecs::World& game_world) override {
        log_->push_back("host.attach_tools");
        tools_world = &game_world;
    }
    void detach_tools() override {
        log_->push_back("host.detach_tools");
        // The panels read the game world: it must still be bound when they let go of it.
        EXPECT_EQ(worlds_->world_for(engine::kPrimaryWindow), tools_world);
        tools_world = nullptr;
    }

    bool fail_load = false;
    engine::ecs::World* tools_world = nullptr;
    std::filesystem::path loaded;
    std::filesystem::path unloaded;

private:
    engine::Worlds* worlds_;
    std::vector<std::string>* log_;
};

class LiveRoot {
public:
    LiveRoot() {
        const auto* const info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = std::filesystem::temp_directory_path() / "wind_editor_tests" / "live" / info->name();
        std::filesystem::remove_all(path_);
    }
    ~LiveRoot() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    LiveRoot(const LiveRoot&) = delete;
    LiveRoot& operator=(const LiveRoot&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

    [[nodiscard]] std::size_t copies() const {
        std::error_code ec;
        if (!std::filesystem::exists(path_, ec)) {
            return 0;
        }
        return static_cast<std::size_t>(std::distance(
                std::filesystem::directory_iterator(path_), std::filesystem::directory_iterator{}));
    }

private:
    std::filesystem::path path_;
};

std::size_t world_count(engine::Worlds& worlds) {
    std::size_t count = 0;
    worlds.each_world([&count](engine::ecs::World&) { ++count; });
    return count;
}

// The editor's side before Play: its own world with its own window. Every fake writes into the
// FixtureLog of that world, which is also where the fixture game writes, so one vector holds the order.
struct EditorSide {
    explicit EditorSide(fakes::Services& services)
        : world(services.worlds.add())
        , log(world.ctx<fixture::FixtureLog>().lines)
        , host(services.worlds, log) {
        services.windows.log = &log;
        services.audio.log = &log;
        window = *services.windows.open_window(engine::WindowDesc{.title = "Wind Editor"});
        services.worlds.bind_window(window, world);
        log.clear();
    }

    engine::ecs::World& world;
    std::vector<std::string>& log;
    RecordingHost host;
    engine::WindowId window{};
};

const engine::WindowDesc kIdle{.title = "Game", .size = {800, 600}};

}

TEST(PlaySession, PlayLoadsTheGameAndAppliesItsWindow) {
    LiveRoot live;
    fakes::Services services;
    EditorSide editor{services};
    editor::PlaySession session{services.services, editor.host, live.path(), kIdle};

    const auto started = session.play(kFixture);

    ASSERT_TRUE(started.has_value()) << started.error();
    EXPECT_NE(started->find("transparent"), std::string::npos) << "transparent cannot change after creation";
    EXPECT_TRUE(session.playing());
    EXPECT_EQ(editor.log, (std::vector<std::string>{"host.load", "game.construct", "windows.title Fixture",
                                  "host.attach", "game.start", "windows.open 2", "host.attach_tools"}));
    ASSERT_NE(editor.host.tools_world, nullptr);
    EXPECT_EQ(editor.host.tools_world, services.worlds.world_for(engine::kPrimaryWindow));
    EXPECT_NE(editor.host.tools_world, &editor.world);
    EXPECT_EQ(editor.host.loaded, kFixture.parent_path() / "assets");
    EXPECT_EQ(services.windows.title, "Fixture");
    EXPECT_EQ(services.windows.primary_size, glm::ivec2(320, 200));
    EXPECT_TRUE(services.windows.always_on_top);
    EXPECT_FALSE(services.windows.vsync());
    EXPECT_EQ(services.windows.max_fps(), 30);
    EXPECT_EQ(world_count(services.worlds), 3u);
    EXPECT_TRUE(services.input.find("fixture_jump").has_value());
    EXPECT_EQ(live.copies(), 1u);
    session.stop();
}

TEST(PlaySession, StopTearsDownInOrderAndUnloadsTheModule) {
    LiveRoot live;
    fakes::Services services;
    EditorSide editor{services};
    editor::PlaySession session{services.services, editor.host, live.path(), kIdle};
    ASSERT_TRUE(session.play(kFixture).has_value());
    editor.log.clear();

    session.stop();

    EXPECT_FALSE(session.playing());
    // The panels let go of the game world first, then on_quit, then kPrimaryWindow lets go of the game,
    // then the game's worlds and windows go, then input, audio, and the catalog. The game object is
    // destroyed last, right before the unload.
    EXPECT_EQ(editor.log, (std::vector<std::string>{"host.detach_tools", "game.quit", "host.detach", "windows.close 2",
                                  "audio.stop_all", "host.unload", "game.destroy", "windows.title Game"}));
    EXPECT_EQ(editor.host.tools_world, nullptr);
    EXPECT_EQ(editor.host.unloaded, editor.host.loaded);
    EXPECT_EQ(world_count(services.worlds), 1u);
    EXPECT_EQ(services.worlds.world_for(editor.window), &editor.world);
    EXPECT_EQ(services.worlds.world_for(engine::kPrimaryWindow), nullptr);
    EXPECT_EQ(services.windows.open_windows(), (std::vector<engine::WindowId>{engine::kPrimaryWindow, editor.window}));
    EXPECT_FALSE(services.input.find("fixture_jump").has_value());
    EXPECT_EQ(services.windows.title, "Game");
    EXPECT_FALSE(services.windows.always_on_top);
    EXPECT_TRUE(services.windows.vsync()) << "the game turned vsync off for the whole process";
    EXPECT_EQ(services.windows.max_fps(), 0);
    EXPECT_EQ(live.copies(), 0u) << "the live copy is deleted on Stop";
}

TEST(PlaySession, PlayAgainAfterStop) {
    LiveRoot live;
    fakes::Services services;
    EditorSide editor{services};
    editor::PlaySession session{services.services, editor.host, live.path(), kIdle};

    ASSERT_TRUE(session.play(kFixture).has_value());
    session.stop();
    session.stop();
    ASSERT_TRUE(session.play(kFixture).has_value());
    EXPECT_TRUE(session.playing());
    EXPECT_EQ(world_count(services.worlds), 3u);
    session.stop();
    EXPECT_EQ(world_count(services.worlds), 1u);
    EXPECT_EQ(live.copies(), 0u);
}

TEST(PlaySession, OtherBuildIdIsAMessageNotAPlay) {
    LiveRoot live;
    fakes::Services services;
    EditorSide editor{services};
    editor::PlaySession session{services.services, editor.host, live.path(), kIdle};

    const auto started = session.play(kWrongBuildId);

    ASSERT_FALSE(started.has_value());
    EXPECT_NE(started.error().find("another engine build"), std::string::npos);
    EXPECT_FALSE(session.playing());
    EXPECT_TRUE(editor.log.empty());
    EXPECT_EQ(world_count(services.worlds), 1u);
    EXPECT_EQ(live.copies(), 0u);
}

TEST(PlaySession, CatalogErrorUnloadsTheModule) {
    LiveRoot live;
    fakes::Services services;
    EditorSide editor{services};
    editor.host.fail_load = true;
    editor::PlaySession session{services.services, editor.host, live.path(), kIdle};

    const auto started = session.play(kFixture);

    ASSERT_FALSE(started.has_value());
    EXPECT_EQ(started.error(), "Game catalog: invalid TOML");
    EXPECT_FALSE(session.playing());
    EXPECT_EQ(editor.log, (std::vector<std::string>{"host.load"}));
    EXPECT_EQ(live.copies(), 0u);
}

TEST(PlaySession, DestructorStops) {
    LiveRoot live;
    fakes::Services services;
    EditorSide editor{services};
    {
        editor::PlaySession session{services.services, editor.host, live.path(), kIdle};
        ASSERT_TRUE(session.play(kFixture).has_value());
    }
    EXPECT_NE(std::ranges::find(editor.log, "game.destroy"), editor.log.end());
    EXPECT_EQ(world_count(services.worlds), 1u);
    EXPECT_EQ(live.copies(), 0u);
}
