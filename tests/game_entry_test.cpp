#include <gtest/gtest.h>

#if defined(ENGINE_WITH_WINDOW)

// This translation unit is compiled the way the editor build compiles a game module.
#define ENGINE_GAME_MODULE 1
#include <engine/game_entry.h>

#include <engine/build_id.h>
#include <engine/core/game_module.h>

#include <cstring>
#include <string_view>

namespace {

class ModuleGame final : public engine::GameBase {
public:
    explicit ModuleGame(const engine::EngineServices& services)
        : engine::GameBase(services.worlds) {}
};

}

ENGINE_GAME(ModuleGame)

TEST(GameEntry, ModuleExportsMatchTheLoaderTypes) {
    const engine::CreateGameFn create = &wind_create_game;
    const engine::DestroyGameFn destroy = &wind_destroy_game;
    const engine::GameBuildIdFn build_id = &wind_game_build_id;
    EXPECT_NE(create, nullptr);
    EXPECT_NE(destroy, nullptr);
    EXPECT_NE(build_id, nullptr);
    EXPECT_EQ(std::string_view{engine::kCreateGameSymbol}, "wind_create_game");
    EXPECT_EQ(std::string_view{engine::kDestroyGameSymbol}, "wind_destroy_game");
    EXPECT_EQ(std::string_view{engine::kGameBuildIdSymbol}, "wind_game_build_id");
}

TEST(GameEntry, ModuleBuildIdIsTheCompiledInConstant) {
    const char* const id = wind_game_build_id();
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(std::strlen(id), engine::kBuildId.size());
    EXPECT_EQ(std::string_view{id}, engine::kBuildId);
}

#else

TEST(GameEntry, ModuleExportsMatchTheLoaderTypes) {
    GTEST_SKIP() << "ENGINE_GAME needs ENGINE_WITH_WINDOW";
}

#endif
