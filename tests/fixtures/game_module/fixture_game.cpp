// A tiny game module for tests/game_module_test.cpp and the editor's play-session test. Built three
// times (CMakeLists.txt): as is, with FIXTURE_WRONG_BUILD_ID, and with FIXTURE_NO_DESTROY.

#include "fixture_log.h"

#include <engine/build_id.h>
#include <engine/core/game_module.h>
#include <engine/core/input_system.h>
#include <engine/core/key_code.h>
#include <engine/core/window_control.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/world_serializer.h>
#include <engine/igame.h>

#include <string>

namespace fixture {

class Game final : public engine::GameBase {
public:
    explicit Game(const engine::EngineServices& services)
        : engine::GameBase(services.worlds)
        , input_(&services.input)
        , windows_(&services.windows) {
        note("construct");
    }

    ~Game() override {
        note("destroy");
    }

    engine::WindowDesc primary_window() const override {
        return engine::WindowDesc{
                .title = "Fixture",
                .size = {320, 200},
                .style = engine::WindowStyle{.always_on_top = true, .transparent = true},
        };
    }

    // Everything a Stop must take down: a second world with a window, an input binding, a system
    // whose code lives in this module, and frame pacing other than the editor's.
    void on_start() override {
        note("start");
        windows_->set_vsync(false);
        windows_->set_max_fps(30);
        engine::ecs::World& tool = worlds().add();
        if (const auto id = windows_->open_window(engine::WindowDesc{.title = "Fixture tool"})) {
            worlds().bind_window(*id, tool);
        }
        input_->bind(engine::KeyCode::Space, input_->intern("fixture_jump"));
        world().add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game, [](engine::ecs::World&) {});
    }

    void on_quit() override {
        note("quit");
    }

private:
    void note(const char* line) {
        engine::ecs::World* first = nullptr;
        worlds().each_world([&first](engine::ecs::World& world) {
            if (first == nullptr) {
                first = &world;
            }
        });
        first->ctx<FixtureLog>().lines.push_back(std::string("game.") + line);
    }

    engine::InputSystem* input_;
    engine::IWindowControl* windows_;
};

}

ENGINE_GAME_EXPORT void wind_register_game_components(engine::ecs::WorldSerializer& serializer) {
    serializer.register_component<fixture::FixtureComponent>(
            "fixture_component",
            [](engine::ecs::ComponentWriter& out, const fixture::FixtureComponent& c) {
                out.field("label", c.label);
                out.field("power", c.power);
            },
            [](engine::ecs::ComponentReader& in, fixture::FixtureComponent& c) {
                (void)in.field("label", c.label);
                (void)in.field("power", c.power);
            });
}

ENGINE_GAME_EXPORT engine::IGame* wind_create_game(const engine::EngineServices& services) {
    auto* const game = new fixture::Game(services);
    wind_register_game_components(game->world().ctx<engine::ecs::WorldSerializer>());
    return game;
}

#if !defined(FIXTURE_NO_DESTROY)
ENGINE_GAME_EXPORT void wind_destroy_game(engine::IGame* game) {
    delete game;
}
#endif

ENGINE_GAME_EXPORT const char* wind_game_build_id() {
#if defined(FIXTURE_WRONG_BUILD_ID)
    return "0000000000000000";
#else
    return engine::kBuildIdCStr;
#endif
}
