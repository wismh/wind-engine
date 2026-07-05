#include <gtest/gtest.h>

#include "edit_module.h"

#include "fixtures/fatal_fakes.h"
#include "fixtures/game_module/fixture_log.h"

#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/resources/assets_db.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kFixture{WIND_FIXTURE_GAME};
const std::filesystem::path kWrongBuildId{WIND_FIXTURE_GAME_WRONG_BUILD_ID};

class RecordingEditHost final : public editor::IPlayHost {
public:
    std::expected<void, std::string> load_catalog(const std::filesystem::path& assets_dir) override {
        log.push_back("host.load");
        loaded = assets_dir;
        if (fail_load) {
            return std::unexpected(std::string("Game catalog: invalid TOML"));
        }
        return {};
    }
    void unload_catalog(const std::filesystem::path& assets_dir) override {
        log.push_back("host.unload");
        unloaded = assets_dir;
    }
    void attach(engine::IGame&) override {}
    void detach() override {}
    void attach_tools(engine::ecs::World&) override {}
    void detach_tools() override {}

    bool fail_load = false;
    std::vector<std::string> log;
    std::filesystem::path loaded;
    std::filesystem::path unloaded;
};

struct EditAssets {
    fakes::QuietFatal fatal;
    engine::AssetsDb assets{fatal};
};

class LiveRoot {
public:
    LiveRoot() {
        const auto* const info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = std::filesystem::temp_directory_path() / "wind_editor_tests" / "edit_live" / info->name();
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

constexpr std::string_view kWorldToml = R"(
version = 1

[[entities]]
id = "beacon"
fixture_component = { label = "edit", power = 42 }
)";

}

TEST(EditModule, LoadRegistersComponentsAndLoadsCatalog) {
    LiveRoot live;
    RecordingEditHost host;
    EditAssets env;
    editor::EditModule edit{host, env.assets, live.path()};

    const auto loaded = edit.load(kFixture);

    ASSERT_TRUE(loaded.has_value()) << loaded.error();
    EXPECT_TRUE(edit.loaded());
    EXPECT_EQ(edit.module_path(), kFixture);
    EXPECT_EQ(host.loaded, kFixture.parent_path() / "assets");
    EXPECT_EQ(host.log, (std::vector<std::string>{"host.load"}));
    EXPECT_EQ(live.copies(), 1u);

    {
        engine::ecs::World scene_world;
        edit.register_components(scene_world);
        EXPECT_NE(scene_world.ctx<engine::ecs::WorldSerializer>().find("fixture_component"), nullptr);

        const auto inst = engine::ecs::load_world_from_toml(scene_world, kWorldToml);
        ASSERT_TRUE(inst.has_value()) << inst.error().message;
        const auto* comp = scene_world.try_get<fixture::FixtureComponent>(inst->by_id.at("beacon"));
        ASSERT_NE(comp, nullptr);
        EXPECT_EQ(comp->label, "edit");
        EXPECT_EQ(comp->power, 42);
    }
}

TEST(EditModule, UnloadResetsSerializersUnloadsCatalogAndDeletesLiveCopy) {
    LiveRoot live;
    RecordingEditHost host;
    engine::ecs::World editor_world;
    EditAssets env;
    editor::EditModule edit{host, env.assets, live.path()};
    ASSERT_TRUE(edit.load(kFixture).has_value());
    edit.register_components(editor_world);
    ASSERT_NE(editor_world.ctx<engine::ecs::WorldSerializer>().find("fixture_component"), nullptr);

    edit.unload();

    EXPECT_FALSE(edit.loaded());
    EXPECT_EQ(edit.module_path(), kFixture) << "kept so Stop can reload after Play";
    EXPECT_EQ(editor_world.ctx<engine::ecs::WorldSerializer>().find("fixture_component"), nullptr);
    EXPECT_NE(editor_world.ctx<engine::ecs::WorldSerializer>().find("transform"), nullptr);
    EXPECT_EQ(host.unloaded, kFixture.parent_path() / "assets");
    EXPECT_EQ(live.copies(), 0u);

    // Without the edit module loaded, a .world file still loads via UnknownComponentMode::Preserve.
    engine::ecs::World fallback_world;
    const auto inst = engine::ecs::load_world_from_toml(
            fallback_world, kWorldToml, nullptr, &editor_world.ctx<engine::ecs::WorldSerializer>(),
            engine::ecs::UnknownComponentMode::Preserve);
    ASSERT_TRUE(inst.has_value()) << inst.error().message;
    const auto* unknown = fallback_world.try_get<engine::ecs::UnknownComponents>(inst->by_id.at("beacon"));
    ASSERT_NE(unknown, nullptr);
    EXPECT_TRUE(unknown->components.contains("fixture_component"));
}

TEST(EditModule, ResetUnloadsAndClearsModulePath) {
    LiveRoot live;
    RecordingEditHost host;
    engine::ecs::World editor_world;
    EditAssets env;
    editor::EditModule edit{host, env.assets, live.path()};
    ASSERT_TRUE(edit.load(kFixture).has_value());
    edit.register_components(editor_world);

    edit.reset(&editor_world);

    EXPECT_FALSE(edit.loaded());
    EXPECT_TRUE(edit.module_path().empty());
    EXPECT_EQ(editor_world.ctx<engine::ecs::WorldSerializer>().find("fixture_component"), nullptr);
    EXPECT_EQ(live.copies(), 0u);
}

TEST(EditModule, MissingModuleOrWrongBuildIdOrCatalogErrorLeavesNothingLoaded) {
    LiveRoot live;
    RecordingEditHost host;
    EditAssets env;
    editor::EditModule edit{host, env.assets, live.path()};

    const auto missing = edit.load(live.path() / "missing.dll");
    ASSERT_FALSE(missing.has_value());
    EXPECT_FALSE(edit.loaded());
    EXPECT_EQ(live.copies(), 0u);

    const auto wrong_id = edit.load(kWrongBuildId);
    ASSERT_FALSE(wrong_id.has_value());
    EXPECT_NE(wrong_id.error().find("another engine build"), std::string::npos);
    EXPECT_FALSE(edit.loaded());
    EXPECT_EQ(live.copies(), 0u);

    host.fail_load = true;
    const auto bad_catalog = edit.load(kFixture);
    ASSERT_FALSE(bad_catalog.has_value());
    EXPECT_EQ(bad_catalog.error(), "Game catalog: invalid TOML");
    EXPECT_FALSE(edit.loaded());
    EXPECT_EQ(live.copies(), 0u);
}
