#include <gtest/gtest.h>

// The loader tests need real game modules, and those exist only in the editor build (CMakeLists.txt
// builds tests/fixtures/game_module/ three ways under ENGINE_EDITOR and passes their paths in).
#if defined(ENGINE_WITH_WINDOW) && defined(WIND_FIXTURE_GAME)

#include "fixtures/fake_services.h"
#include "fixtures/game_module/fixture_log.h"

#include <engine/build_id.h>
#include <engine/core/build_info.h>
#include <engine/core/game_module.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// A fresh live root under the system temp directory, deleted at the end of the test.
class LiveRoot {
public:
    LiveRoot() {
        const auto* const info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = std::filesystem::temp_directory_path() / "wind_engine_tests" / "live" / info->name();
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

private:
    std::filesystem::path path_;
};

std::size_t entry_count(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return 0;
    }
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        (void)entry;
        ++count;
    }
    return count;
}

const std::filesystem::path kFixture{WIND_FIXTURE_GAME};
const std::filesystem::path kWrongBuildId{WIND_FIXTURE_GAME_WRONG_BUILD_ID};
const std::filesystem::path kNoDestroy{WIND_FIXTURE_GAME_NO_DESTROY};

}

TEST(GameModule, LoadsACopyCreatesAndDestroysTheGame) {
    LiveRoot live;
    fakes::Services services;
    engine::ecs::World& keep = services.worlds.add();
    keep.ctx<fixture::FixtureLog>();

    auto loaded = engine::load_game_module(kFixture, live.path());
    ASSERT_TRUE(loaded.has_value()) << engine::describe(loaded.error());
    engine::GameModule& module = *loaded;
    EXPECT_EQ(module.build_id(), engine::build_id());
    EXPECT_EQ(module.source_path(), kFixture);
    EXPECT_EQ(module.live_path().filename(), kFixture.filename());
    EXPECT_EQ(module.live_path().parent_path().parent_path(), live.path());
    EXPECT_TRUE(std::filesystem::is_regular_file(module.live_path()));
    // A Release fixture has no .pdb to copy; when the source has one, the copy sits beside the module.
    std::filesystem::path source_pdb = kFixture;
    source_pdb.replace_extension(".pdb");
    std::filesystem::path pdb = module.live_path();
    pdb.replace_extension(".pdb");
    EXPECT_EQ(std::filesystem::is_regular_file(pdb), std::filesystem::is_regular_file(source_pdb))
            << "the .pdb is copied beside the module when the source has one";

    engine::IGame* const game = module.create(services.services);
    ASSERT_NE(game, nullptr);
    game->on_start();
    game->on_quit();
    fakes::destroy_worlds_except(services.worlds, keep);
    module.destroy(game);

    EXPECT_EQ(keep.ctx<fixture::FixtureLog>().lines,
            (std::vector<std::string>{"game.construct", "game.start", "game.quit", "game.destroy"}));
}

TEST(GameModule, UnloadDeletesTheCopy) {
    LiveRoot live;
    std::filesystem::path live_dir;
    {
        auto loaded = engine::load_game_module(kFixture, live.path());
        ASSERT_TRUE(loaded.has_value()) << engine::describe(loaded.error());
        live_dir = loaded->live_path().parent_path();
        EXPECT_TRUE(std::filesystem::is_directory(live_dir));
    }
    EXPECT_FALSE(std::filesystem::exists(live_dir));
    EXPECT_EQ(entry_count(live.path()), 0u);
}

TEST(GameModule, EachLoadGetsItsOwnDirectory) {
    LiveRoot live;
    auto first = engine::load_game_module(kFixture, live.path());
    auto second = engine::load_game_module(kFixture, live.path());
    ASSERT_TRUE(first.has_value()) << engine::describe(first.error());
    ASSERT_TRUE(second.has_value()) << engine::describe(second.error());
    EXPECT_NE(first->live_path().parent_path(), second->live_path().parent_path());
    EXPECT_EQ(entry_count(live.path()), 2u);
}

TEST(GameModule, MovedModuleOwnsTheCopy) {
    LiveRoot live;
    auto loaded = engine::load_game_module(kFixture, live.path());
    ASSERT_TRUE(loaded.has_value()) << engine::describe(loaded.error());
    const std::filesystem::path live_dir = loaded->live_path().parent_path();
    {
        engine::GameModule moved = std::move(*loaded);
        EXPECT_TRUE(std::filesystem::is_directory(live_dir));
    }
    EXPECT_FALSE(std::filesystem::exists(live_dir));
}

TEST(GameModule, MissingFile) {
    LiveRoot live;
    const auto loaded = engine::load_game_module(live.path() / "no_such_game.dll", live.path());
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().kind, engine::ModuleError::Missing);
    EXPECT_EQ(entry_count(live.path()), 0u);
}

TEST(GameModule, NotALibrary) {
    LiveRoot live;
    std::filesystem::create_directories(live.path());
    const std::filesystem::path fake = live.path().parent_path() / "not_a_game.dll";
    std::ofstream(fake) << "not a module";
    const auto loaded = engine::load_game_module(fake, live.path());
    std::filesystem::remove(fake);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().kind, engine::ModuleError::LoadFailed);
    EXPECT_EQ(entry_count(live.path()), 0u) << "a failed load deletes its copy";
}

TEST(GameModule, MissingExportIsRefused) {
    LiveRoot live;
    const auto loaded = engine::load_game_module(kNoDestroy, live.path());
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().kind, engine::ModuleError::MissingSymbol);
    EXPECT_EQ(loaded.error().detail, engine::kDestroyGameSymbol);
    EXPECT_EQ(entry_count(live.path()), 0u);
}

TEST(GameModule, OtherBuildIdIsRefused) {
    LiveRoot live;
    const auto loaded = engine::load_game_module(kWrongBuildId, live.path());
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().kind, engine::ModuleError::BuildIdMismatch);
    EXPECT_NE(loaded.error().detail.find("0000000000000000"), std::string::npos);
    EXPECT_NE(loaded.error().detail.find(std::string(engine::build_id())), std::string::npos);
    EXPECT_NE(engine::describe(loaded.error()).find("another engine build"), std::string::npos);
    EXPECT_EQ(entry_count(live.path()), 0u);
}

TEST(GameModule, PurgeRemovesStaleCopies) {
    LiveRoot live;
    std::filesystem::create_directories(live.path() / "1");
    std::filesystem::create_directories(live.path() / "7");
    std::ofstream(live.path() / "7" / "old_game.dll") << "stale";

    EXPECT_EQ(engine::purge_game_module_copies(live.path()), 2u);
    EXPECT_EQ(entry_count(live.path()), 0u);
    EXPECT_EQ(engine::purge_game_module_copies(live.path() / "missing"), 0u);
}

#else

TEST(GameModule, NeedsTheEditorBuild) {
    GTEST_SKIP() << "game module fixtures are built only with ENGINE_EDITOR";
}

#endif
