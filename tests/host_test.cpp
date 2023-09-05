#include <gtest/gtest.h>

#include <engine/audio/audio_system.h>
#include <engine/audio/sound.h>
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

    void draw() override {
        ++draw_count;
    }
};

class DummyGame final : public engine::GameBase {
public:
    int draw_calls = 0;
    int quit_calls = 0;

    void on_draw() override {
        ++draw_calls;
    }

    void on_quit() override {
        ++quit_calls;
    }
};

class SequenceGame final : public engine::GameBase {
public:
    std::vector<std::string> seq;
    bool engine_registered_on_start = false;
    int game_ticks = 0;

    void on_start() override {
        engine_registered_on_start = world().ctx<engine::EngineSystemsRegistered>().value;
        seq.push_back("on_start");
        world().add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game, [this](engine::ecs::World&) {
            seq.push_back("Game");
            ++game_ticks;
        });
    }
};

class WindowSizeGame final : public engine::GameBase {
public:
    engine::ui::WindowSize seen{};

    glm::ivec2 window_size() const override {
        return {800, 600};
    }

    void on_start() override {
        seen = world().ctx<engine::ui::WindowSize>();
    }
};

class FillWindowGame final : public engine::GameBase {
public:
    engine::ecs::Entity canvas_entity{};

    glm::ivec2 window_size() const override {
        return {640, 480};
    }

    void on_start() override {
        canvas_entity = world().create();
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::FillWindow;
        canvas.rect = engine::render::Rect{1.0f, 2.0f, 3.0f, 4.0f};
        world().emplace<engine::ui::UiCanvas>(canvas_entity, canvas);
    }
};

void record_phase(std::vector<std::string>& log, const char* name) {
    log.push_back(name);
}

class CountingAudio final : public engine::IAudioSystem {
public:
    int update_count = 0;
    float last_dt = 0.f;

    bool init() override {
        return true;
    }

    void dispose() override {}

    void update(float dt) override {
        ++update_count;
        last_dt = dt;
    }

    void play_sfx(const engine::Sound&, float) override {}
    void play_music(const engine::Sound&, bool, float) override {}
    void stop_music(float) override {}
    bool is_music_playing() const override {
        return false;
    }

    engine::LoopingSfxHandle create_looping_sfx() override {
        return {};
    }

    void play_looping_sfx(engine::LoopingSfxHandle, const engine::Sound&, float) override {}
    void stop_looping_sfx(engine::LoopingSfxHandle, float) override {}
    void release_looping_sfx(engine::LoopingSfxHandle, float) override {}
    void set_master_volume(float) override {}
    void set_music_volume(float) override {}
    void set_sfx_volume(float) override {}
};

}

TEST(Host, WorldOnIGame) {
    DummyGame game;
    FakeCanvas canvas;
    engine::Host host{game, canvas};

    EXPECT_EQ(&game.world(), &host.world());
}

TEST(Host, RegisterEngineSystemsBeforeOnStart) {
    SequenceGame game;
    FakeCanvas canvas;
    engine::Host host{game, canvas};

    ASSERT_TRUE(game.engine_registered_on_start);
    ASSERT_EQ(game.seq, (std::vector<std::string>{"on_start"}));

    host.tick();

    EXPECT_EQ(game.game_ticks, 1);
    EXPECT_EQ(game.seq, (std::vector<std::string>{"on_start", "Game"}));
}

TEST(Host, PhaseOrderFrame) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    std::vector<std::string> order;
    world.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Input,
            [&](engine::ecs::World&) { record_phase(order, "Input"); });
    world.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [&](engine::ecs::World&) { record_phase(order, "Game"); });
    world.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Bind,
            [&](engine::ecs::World&) { record_phase(order, "Bind"); });
    world.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Audio,
            [&](engine::ecs::World&) { record_phase(order, "Audio"); });
    world.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Render,
            [&](engine::ecs::World&) { record_phase(order, "Render"); });
    world.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::UiRender,
            [&](engine::ecs::World&) { record_phase(order, "UiRender"); });

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(order, (std::vector<std::string>{"Input", "Game", "Bind", "Audio", "Render", "UiRender"}));
}

TEST(Host, PhaseOrderFixed) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    std::vector<std::string> order;
    world.add_system(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Physics,
            [&](engine::ecs::World&) { record_phase(order, "Physics"); });
    world.add_system(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Game,
            [&](engine::ecs::World&) { record_phase(order, "Game"); });

    world.run(engine::ecs::Schedule::Fixed);

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
    EXPECT_EQ(host.world().ctx<engine::ui::WindowSize>().width, game.window_size().x);
    EXPECT_EQ(host.world().ctx<engine::ui::WindowSize>().height, game.window_size().y);
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

TEST(Host, AudioUpdateEveryFrame) {
    DummyGame game;
    FakeCanvas canvas;
    CountingAudio audio;
    engine::Host host{game, canvas, &audio};

    host.tick(engine::kFixed);
    EXPECT_EQ(audio.update_count, 1);
    EXPECT_FLOAT_EQ(audio.last_dt, engine::kFixed);

    host.application_state().paused = true;
    host.tick(0.05f);
    EXPECT_EQ(audio.update_count, 2);
    EXPECT_FLOAT_EQ(audio.last_dt, 0.05f);
}
