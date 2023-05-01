#include <gtest/gtest.h>

#include <engine/core/host.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/igame.h>
#include <engine/render/canvas.h>
#include <engine/ui/canvas.h>

#include <string>
#include <vector>

namespace {

class FakeCanvas final : public engine::render::ICanvas {
public:
    int draw_count = 0;

    void Draw() override {
        ++draw_count;
    }
};

class DummyGame final : public engine::GameBase {
public:
    int draw_calls = 0;
    int quit_calls = 0;

    void OnDraw() override {
        ++draw_calls;
    }

    void OnQuit() override {
        ++quit_calls;
    }
};

class SequenceGame final : public engine::GameBase {
public:
    std::vector<std::string> seq;
    bool engine_registered_on_start = false;
    int game_ticks = 0;

    void OnStart() override {
        engine_registered_on_start = World().ctx<engine::EngineSystemsRegistered>().value;
        seq.push_back("OnStart");
        World().AddSystem(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game, [this](engine::ecs::World&) {
            seq.push_back("Game");
            ++game_ticks;
        });
    }
};

class WindowSizeGame final : public engine::GameBase {
public:
    engine::ui::WindowSize seen{};

    glm::ivec2 WindowSize() const override {
        return {800, 600};
    }

    void OnStart() override {
        seen = World().ctx<engine::ui::WindowSize>();
    }
};

class FillWindowGame final : public engine::GameBase {
public:
    engine::ecs::Entity canvas_entity{};

    glm::ivec2 WindowSize() const override {
        return {640, 480};
    }

    void OnStart() override {
        canvas_entity = World().create();
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::FillWindow;
        canvas.rect = engine::render::Rect{1.0f, 2.0f, 3.0f, 4.0f};
        World().emplace<engine::ui::UiCanvas>(canvas_entity, canvas);
    }
};

void record_phase(std::vector<std::string>& log, const char* name) {
    log.push_back(name);
}

}

TEST(Host, WorldOnIGame) {
    DummyGame game;
    FakeCanvas canvas;
    engine::Host host{game, canvas};

    EXPECT_EQ(&game.World(), &host.world());
}

TEST(Host, RegisterEngineSystemsBeforeOnStart) {
    SequenceGame game;
    FakeCanvas canvas;
    engine::Host host{game, canvas};

    ASSERT_TRUE(game.engine_registered_on_start);
    ASSERT_EQ(game.seq, (std::vector<std::string>{"OnStart"}));

    host.tick();

    EXPECT_EQ(game.game_ticks, 1);
    EXPECT_EQ(game.seq, (std::vector<std::string>{"OnStart", "Game"}));
}

TEST(Host, PhaseOrderFrame) {
    engine::ecs::World world;
    engine::RegisterEngineSystems(world);

    std::vector<std::string> order;
    world.AddSystem(engine::ecs::Schedule::Frame, engine::ecs::Phase::Input,
            [&](engine::ecs::World&) { record_phase(order, "Input"); });
    world.AddSystem(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [&](engine::ecs::World&) { record_phase(order, "Game"); });
    world.AddSystem(engine::ecs::Schedule::Frame, engine::ecs::Phase::Bind,
            [&](engine::ecs::World&) { record_phase(order, "Bind"); });
    world.AddSystem(engine::ecs::Schedule::Frame, engine::ecs::Phase::Audio,
            [&](engine::ecs::World&) { record_phase(order, "Audio"); });
    world.AddSystem(engine::ecs::Schedule::Frame, engine::ecs::Phase::Render,
            [&](engine::ecs::World&) { record_phase(order, "Render"); });
    world.AddSystem(engine::ecs::Schedule::Frame, engine::ecs::Phase::UiRender,
            [&](engine::ecs::World&) { record_phase(order, "UiRender"); });

    world.Run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(order, (std::vector<std::string>{"Input", "Game", "Bind", "Audio", "Render", "UiRender"}));
}

TEST(Host, PhaseOrderFixed) {
    engine::ecs::World world;
    engine::RegisterEngineSystems(world);

    std::vector<std::string> order;
    world.AddSystem(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Physics,
            [&](engine::ecs::World&) { record_phase(order, "Physics"); });
    world.AddSystem(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Game,
            [&](engine::ecs::World&) { record_phase(order, "Game"); });

    world.Run(engine::ecs::Schedule::Fixed);

    EXPECT_EQ(order, (std::vector<std::string>{"Physics", "Game"}));
}

TEST(Host, FakeCanvasDraw) {
    DummyGame game;
    FakeCanvas canvas;
    engine::Host host{game, canvas};

    EXPECT_EQ(canvas.draw_count, 0);

    host.tick();

    EXPECT_EQ(canvas.draw_count, 1);
    EXPECT_EQ(game.draw_calls, 0);
}

TEST(Host, WindowSizeWrittenBeforeOnStart) {
    WindowSizeGame game;
    FakeCanvas canvas;
    engine::Host host{game, canvas};

    EXPECT_EQ(game.seen.width, 800);
    EXPECT_EQ(game.seen.height, 600);
    EXPECT_EQ(host.world().ctx<engine::ui::WindowSize>().width, game.WindowSize().x);
    EXPECT_EQ(host.world().ctx<engine::ui::WindowSize>().height, game.WindowSize().y);
}

TEST(Host, ResizeWritesWindowSize) {
    FillWindowGame game;
    FakeCanvas canvas;
    engine::Host host{game, canvas};

    const engine::ui::UiCanvas& before = host.world().get<engine::ui::UiCanvas>(game.canvas_entity);
    EXPECT_EQ(before.rect, (engine::render::Rect{0.0f, 0.0f, 640.0f, 480.0f}));

    host.resize(1024, 768);

    const engine::ui::WindowSize& size = host.world().ctx<engine::ui::WindowSize>();
    EXPECT_EQ(size.width, 1024);
    EXPECT_EQ(size.height, 768);

    const engine::ui::UiCanvas& after = host.world().get<engine::ui::UiCanvas>(game.canvas_entity);
    EXPECT_EQ(after.rect, (engine::render::Rect{0.0f, 0.0f, 1024.0f, 768.0f}));

    bool saw_resize = false;
    engine::ecs::EventReader<engine::ui::WindowResizeEvent> reader{host.world()};
    for (const engine::ui::WindowResizeEvent& event : reader) {
        if (event.width == 1024 && event.height == 768) {
            saw_resize = true;
        }
    }
    EXPECT_TRUE(saw_resize);
}
