#include <gtest/gtest.h>

#include "core/frame_step.h"

#include <engine/core/input_system.h>
#include <engine/core/time.h>
#include <engine/core/worlds.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/render/command_buffer.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/canvas.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include "fixtures/fatal_fakes.h"

namespace {

class FakeMesh final : public engine::render::IMesh {};

class FakeMaterial final : public engine::render::IMaterial {
public:
    std::shared_ptr<engine::render::IShader> shader() const override {
        return {};
    }
    std::shared_ptr<engine::render::ITexture> texture(int) const override {
        return {};
    }
    glm::vec4 color() const override {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }
    engine::render::RenderState render_state() const override {
        return {.blend = engine::render::BlendMode::Opaque};
    }
};

int read_mouse_count(engine::ecs::World& world) {
    int count = 0;
    for (const engine::MouseEvent& event : engine::ecs::EventReader<engine::MouseEvent>{world}) {
        (void)event;
        ++count;
    }
    return count;
}

void spawn_drawable(engine::ecs::World& world, const std::shared_ptr<engine::render::IMesh>& mesh,
        const std::shared_ptr<engine::render::IMaterial>& material) {
    const engine::ecs::Entity camera = world.create();
    world.emplace<engine::Transform>(camera, engine::Transform{});
    world.emplace<engine::Camera>(camera, engine::Camera{});
    world.ctx<engine::ActiveCamera>().entity = camera;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::render::Renderable>(entity, engine::render::Renderable{mesh, material});
}

}

TEST(Worlds, EventInOneWorldIsNotVisibleInAnother) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& first = worlds.add();
    engine::ecs::World& second = worlds.add();

    engine::ecs::EventWriter<engine::MouseEvent>{first}.send(engine::MouseEvent{});

    EXPECT_EQ(read_mouse_count(first), 1);
    EXPECT_EQ(read_mouse_count(second), 0);
}

TEST(Worlds, WindowEventGoesToTheBoundWorld) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& first = worlds.add();
    engine::ecs::World& second = worlds.add();
    const engine::WindowId second_window{1};
    worlds.bind_window(engine::kPrimaryWindow, first);
    worlds.bind_window(second_window, second);

    engine::InputSystem input;
    input.set_router([&](engine::WindowId id) { return worlds.world_for(id); });
    input.handle_mouse_button(second_window, engine::MouseButton::Left, true, {3.0f, 4.0f});
    input.handle_mouse_button(engine::WindowId{9}, engine::MouseButton::Left, true, {0.0f, 0.0f});

    EXPECT_EQ(read_mouse_count(first), 0);
    EXPECT_EQ(read_mouse_count(second), 1);
}

TEST(Worlds, SteppingFalseSkipsFixed) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& first = worlds.add();
    engine::ecs::World& second = worlds.add();
    int first_ticks = 0;
    int second_ticks = 0;
    first.add_system(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Physics,
            [&](engine::ecs::World&) { ++first_ticks; });
    second.add_system(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Physics,
            [&](engine::ecs::World&) { ++second_ticks; });
    worlds.set_stepping(second, false);
    int first_frames = 0;
    int second_frames = 0;
    first.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [&](engine::ecs::World&) { ++first_frames; });
    second.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [&](engine::ecs::World&) { ++second_frames; });

    engine::simulate_worlds(worlds, nullptr, engine::kFixed);

    EXPECT_EQ(first_ticks, 1);
    EXPECT_EQ(second_ticks, 0);
    EXPECT_EQ(first_frames, 1);
    EXPECT_EQ(second_frames, 0);
}

TEST(Worlds, SecondBindOnTheSameWindowIsFatal) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& first = worlds.add();
    engine::ecs::World& second = worlds.add();
    worlds.bind_window(engine::kPrimaryWindow, first);

    EXPECT_THROW(worlds.bind_window(engine::kPrimaryWindow, second), std::runtime_error);
    EXPECT_EQ(worlds.world_for(engine::kPrimaryWindow), &first);
}

TEST(Worlds, MeshesGoToEachBoundWindowWithThatWindowSize) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::render::CommandBuffer primary_buffer;
    engine::render::CommandBuffer guest_buffer;
    const engine::WindowId guest_window{2};
    engine::EngineSystemDeps deps;
    deps.commands_for_window = [&](engine::WindowId id) -> engine::render::CommandBuffer* {
        if (id == engine::kPrimaryWindow) {
            return &primary_buffer;
        }
        if (id == guest_window) {
            return &guest_buffer;
        }
        return nullptr;
    };
    worlds.set_deps(deps);

    engine::ecs::World& primary = worlds.add();
    engine::ecs::World& guest = worlds.add();
    worlds.bind_window(engine::kPrimaryWindow, primary);
    worlds.bind_window(guest_window, guest);
    worlds.presentation().sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    worlds.presentation().sizes.sizes[guest_window] = engine::ui::WindowSize{200, 600};

    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    spawn_drawable(primary, mesh, material);
    spawn_drawable(guest, mesh, material);

    engine::simulate_worlds(worlds, nullptr, engine::kFixed);

    ASSERT_EQ(primary_buffer.size(), 2u);
    ASSERT_EQ(guest_buffer.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(primary_buffer[1]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(guest_buffer[1]));
    const auto& primary_draw = std::get<engine::render::CmdSetView>(primary_buffer[0]);
    const auto& guest_draw = std::get<engine::render::CmdSetView>(guest_buffer[0]);
    const engine::Camera camera{};
    EXPECT_EQ(primary_draw.projection, engine::projection_matrix(camera, engine::ui::WindowSize{800, 600}));
    EXPECT_EQ(guest_draw.projection, engine::projection_matrix(camera, engine::ui::WindowSize{200, 600}));
    EXPECT_NE(primary_draw.projection, guest_draw.projection);
}

// One world on two windows: each buffer gets exactly one CmdSetView, first, with that window's projection, then
// the same sorted draws.
TEST(Worlds, OneWorldOnTwoWindowsPushesOneViewPerWindowBeforeItsDraws) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::render::CommandBuffer primary_buffer;
    engine::render::CommandBuffer float_buffer;
    const engine::WindowId float_window{2};
    engine::EngineSystemDeps deps;
    deps.commands_for_window = [&](engine::WindowId id) -> engine::render::CommandBuffer* {
        if (id == engine::kPrimaryWindow) {
            return &primary_buffer;
        }
        return id == float_window ? &float_buffer : nullptr;
    };
    worlds.set_deps(deps);

    engine::ecs::World& world = worlds.add();
    worlds.bind_window(engine::kPrimaryWindow, world);
    worlds.bind_window(float_window, world);
    worlds.presentation().sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    worlds.presentation().sizes.sizes[float_window] = engine::ui::WindowSize{300, 600};

    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    spawn_drawable(world, mesh, material);
    const engine::ecs::Entity second = world.create();
    world.emplace<engine::Transform>(second, engine::Transform{.position = {1.0f, 0.0f, 0.0f}});
    world.emplace<engine::render::Renderable>(second, engine::render::Renderable{mesh, material});
    engine::Camera& camera = world.get<engine::Camera>(world.ctx<engine::ActiveCamera>().entity);
    camera.projection = engine::Projection::Perspective;
    camera.near_clip = 0.1f;
    camera.far_clip = 100.0f;

    engine::simulate_worlds(worlds, nullptr, engine::kFixed);

    const auto check = [&](const engine::render::CommandBuffer& buffer, engine::ui::WindowSize size) {
        ASSERT_EQ(buffer.size(), 3u);
        int views = 0;
        for (const engine::render::Command& command : buffer) {
            views += std::holds_alternative<engine::render::CmdSetView>(command) ? 1 : 0;
        }
        EXPECT_EQ(views, 1);
        ASSERT_TRUE(std::holds_alternative<engine::render::CmdSetView>(buffer[0]));
        EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[1]));
        EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[2]));
        const auto& view = std::get<engine::render::CmdSetView>(buffer[0]);
        EXPECT_EQ(view.projection, engine::projection_matrix(camera, size));
        EXPECT_EQ(view.view, glm::mat4(1.0f));
    };
    check(primary_buffer, engine::ui::WindowSize{800, 600});
    check(float_buffer, engine::ui::WindowSize{300, 600});
    EXPECT_EQ(std::get<engine::render::CmdSetView>(primary_buffer[0]).lights,
            std::get<engine::render::CmdSetView>(float_buffer[0]).lights);
}

TEST(Worlds, WindowlessWorldDoesNotTouchACommandBuffer) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::render::CommandBuffer buffer;
    buffer.push(engine::render::CmdDrawMesh{});
    engine::EngineSystemDeps deps;
    deps.commands = &buffer;
    worlds.set_deps(deps);
    engine::ecs::World& world = worlds.add();
    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    spawn_drawable(world, mesh, material);

    engine::simulate_worlds(worlds, nullptr, engine::kFixed);

    EXPECT_EQ(buffer.size(), 1u);
}

TEST(Worlds, DestroyUnbindsAndTheOtherWorldKeepsStepping) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& first = worlds.add();
    engine::ecs::World& second = worlds.add();
    worlds.bind_window(engine::kPrimaryWindow, first);
    int ticks = 0;
    second.add_system(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Physics,
            [&](engine::ecs::World&) { ++ticks; });

    worlds.destroy(first);

    EXPECT_EQ(worlds.world_for(engine::kPrimaryWindow), nullptr);
    engine::simulate_worlds(worlds, nullptr, engine::kFixed);
    EXPECT_EQ(ticks, 1);
}

TEST(Worlds, OsPauseRunsFrameOnlyForWorldsWithAWindow) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& windowed = worlds.add();
    engine::ecs::World& windowless = worlds.add();
    worlds.bind_window(engine::kPrimaryWindow, windowed);
    int windowed_frames = 0;
    int windowless_frames = 0;
    int windowed_fixed = 0;
    windowed.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [&](engine::ecs::World&) { ++windowed_frames; });
    windowless.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [&](engine::ecs::World&) { ++windowless_frames; });
    windowed.add_system(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Physics,
            [&](engine::ecs::World&) { ++windowed_fixed; });
    worlds.application_state().paused = true;

    engine::simulate_worlds(worlds, nullptr, engine::kFixed);

    EXPECT_EQ(windowed_frames, 1);
    EXPECT_EQ(windowless_frames, 0);
    EXPECT_EQ(windowed_fixed, 0);
}

TEST(Worlds, BeginFrameDoesNotClearSharedMouseConsumption) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::ecs::World& first = worlds.add();
    engine::ecs::World& second = worlds.add();
    worlds.enable_ui(first);
    worlds.enable_ui(second);
    worlds.presentation().mouse.consumed_windows.insert(engine::kPrimaryWindow);

    engine::ui::begin_frame(first);
    engine::ui::begin_frame(second);

    EXPECT_TRUE(worlds.presentation().mouse.consumed_for(engine::kPrimaryWindow));
}

TEST(Worlds, SystemsRegisterOnceWhetherTheWorldComesBeforeOrAfterSetDeps) {
    fakes::QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::render::CommandBuffer early_buffer;
    engine::render::CommandBuffer late_buffer;
    const engine::WindowId late_window{2};

    // Same order as EngineHost: the game world exists before set_deps; a world added later
    // registers on add. Both then get enable_ui and enable_audio, twice to prove idempotence.
    engine::ecs::World& early = worlds.add();
    engine::EngineSystemDeps deps;
    deps.commands_for_window = [&](engine::WindowId id) -> engine::render::CommandBuffer* {
        if (id == engine::kPrimaryWindow) {
            return &early_buffer;
        }
        if (id == late_window) {
            return &late_buffer;
        }
        return nullptr;
    };
    worlds.set_deps(deps);
    worlds.set_deps(deps);
    engine::ecs::World& late = worlds.add();
    for (engine::ecs::World* world : {&early, &late}) {
        worlds.enable_ui(*world);
        worlds.enable_audio(*world);
        worlds.enable_ui(*world);
        worlds.enable_audio(*world);
    }
    worlds.bind_window(engine::kPrimaryWindow, early);
    worlds.bind_window(late_window, late);
    worlds.presentation().sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    worlds.presentation().sizes.sizes[late_window] = engine::ui::WindowSize{800, 600};

    EXPECT_TRUE(early.ctx<engine::SimulationSystemsRegistered>().value);
    EXPECT_TRUE(early.ctx<engine::UiSystemsRegistered>().value);
    EXPECT_TRUE(early.ctx<engine::AudioSystemsRegistered>().value);
    EXPECT_TRUE(late.ctx<engine::SimulationSystemsRegistered>().value);

    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    spawn_drawable(early, mesh, material);
    spawn_drawable(late, mesh, material);

    engine::simulate_worlds(worlds, nullptr, engine::kFixed);

    // run_render registered twice would push the view and the mesh twice.
    EXPECT_EQ(early_buffer.size(), 2u);
    EXPECT_EQ(late_buffer.size(), 2u);
}
