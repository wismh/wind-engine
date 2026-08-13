#include <gtest/gtest.h>

#include "cli/cli_server.h"
#include "cli/json.h"
#include "ui/dock_runtime.h"

#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/dock_layout.h>
#include <engine/ui/dock_space.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>

#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(ENGINE_CLI_SERVER)
#include "fixtures/cli_client.h"
#endif

// `wind-cli dock`: list a world's dock spaces, activate a tab, move or float a panel, switch the float mode.
// docs/tech/features/CLI.md#dock.

namespace {

    using engine::kPrimaryWindow;
    using engine::render::Rect;
    using engine::ui::DockFloatMode;
    using engine::ui::DockLayout;
    using engine::ui::DockNodeId;
    using engine::ui::DockSpace;
    using engine::ui::DockZone;
    using engine::ui::UiCanvas;

    constexpr Rect kWindow{0.0f, 0.0f, 800.0f, 600.0f};

    DockNodeId stack_of(const DockLayout &layout, std::string_view key) { return layout.find(key)->stack; }

    // `a` and `c` tabbed on the left (`c` active), `b` on the right.
    DockLayout side_by_side() {
        DockLayout layout;
        layout.add("a", {});
        layout.add("b", {stack_of(layout, "a"), DockZone::Right});
        layout.add("c", {stack_of(layout, "a"), DockZone::Center});
        return layout;
    }

    // `a` docked; `b` and `c` in two floats, `c` on top.
    DockLayout two_floats() {
        DockLayout layout;
        layout.add("a", {});
        layout.add("b", {stack_of(layout, "a"), DockZone::Right});
        layout.add("c", {stack_of(layout, "a"), DockZone::Right});
        layout.float_panel("b", Rect{100.0f, 100.0f, 300.0f, 200.0f});
        layout.float_panel("c", Rect{200.0f, 150.0f, 300.0f, 200.0f});
        return layout;
    }

    // The JSON string `text` is written as, quotes included.
    std::string json_string(std::string_view text) {
        engine::cli::Json json;
        json.string(text);
        return json.str();
    }

    class CliDock : public ::testing::Test {
    protected:
        engine::render::CommandBuffer commands;
        engine::ecs::World world;
        std::vector<engine::ecs::Entity> panel_canvases;

        void SetUp() override {
            engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
            engine::ui::presentation_of(world).sizes.sizes[kPrimaryWindow] = engine::ui::WindowSize{800, 600};
        }

        // A space over the whole window with panels a, b, c registered, and one frame run.
        engine::ecs::Entity make_space(DockLayout layout, int order = 10) {
            DockSpace space;
            space.area = kWindow;
            space.order = order;
            space.layout = std::move(layout);
            for (const char *key: {"a", "b", "c"}) {
                UiCanvas canvas;
                canvas.order = 500;
                const engine::ecs::Entity entity =
                        engine::ui::spawn_canvas(world, canvas, *engine::ui::make_document(engine::ui::canvas()));
                panel_canvases.push_back(entity);
                space.panels.push_back(engine::ui::DockPanel{key, std::string("Panel ") + key, entity});
            }
            const engine::ecs::Entity entity = world.create();
            world.emplace<DockSpace>(entity, std::move(space));
            frame();
            return entity;
        }

        void frame() {
            engine::ui::reset_pointer_frame(engine::ui::presentation_of(world));
            world.run(engine::ecs::Schedule::Frame);
            world.flush_events();
        }

        // The body a request answers, the request parsed from its wire form.
        std::string dock(std::string_view body) {
            const engine::cli::CliRequest request = engine::cli::parse_request(body);
            EXPECT_TRUE(request.error.empty()) << body;
            return engine::cli::execute(world, request).json;
        }

        DockSpace &space(engine::ecs::Entity entity) { return world.get<DockSpace>(entity); }
    };

#if defined(ENGINE_CLI_SERVER)

    class CliDockLoopback : public CliDock {
    protected:
        void SetUp() override {
            CliDock::SetUp();
            engine::cli::start();
        }
        void TearDown() override { engine::cli::stop(); }
    };

#endif

} // namespace

TEST(CliDockParse, ReadsTheDockFields) {
    const engine::cli::CliRequest request = engine::cli::parse_request(
            R"({"command":"dock","action":"move","panel":"a","node":3,"zone":"left","mode":"os","space":1,)"
            R"("x":1,"y":2,"w":30,"h":40})");
    ASSERT_TRUE(request.error.empty());
    EXPECT_TRUE(engine::cli::is_ui_command(request.command));
    EXPECT_EQ(request.action, "move");
    EXPECT_EQ(request.panel, "a");
    EXPECT_EQ(request.node, 3u);
    EXPECT_EQ(request.zone, "left");
    EXPECT_EQ(request.mode, "os");
    EXPECT_EQ(request.space, 1u);
    EXPECT_TRUE(request.has_w && request.has_h);
    EXPECT_EQ(request.w, 30.0);
    EXPECT_EQ(request.h, 40.0);

    EXPECT_EQ(engine::cli::parse_request(R"({"command":"dock","node":-1})").error, "invalid request");
    EXPECT_EQ(engine::cli::parse_request(R"({"command":"dock","space":1.5})").error, "invalid request");
    EXPECT_EQ(engine::cli::parse_request(R"({"command":"dock","panel":3})").error, "invalid request");
}

TEST_F(CliDock, ListsSpacesNodesPanelsAndTheLayoutText) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const DockLayout &layout = space(entity).layout;

    const std::string body = dock(R"({"command":"dock"})");
    EXPECT_TRUE(body.starts_with(R"({"ok":true,"result":{"spaces":[{"space":0,"window":0,"order":10,)")) << body;
    EXPECT_NE(body.find(R"("area":{"x":0,"y":0,"w":800,"h":600})"), std::string::npos) << body;
    EXPECT_NE(body.find(R"("float_mode":"virtual","revision":0,"gesture":false)"), std::string::npos) << body;
    EXPECT_NE(body.find(std::format(R"("root":{})", layout.root())), std::string::npos) << body;
    EXPECT_NE(body.find(std::format(R"({{"id":{},"parent":{},"float":0,"kind":"tabs","panels":["a","c"],)"
                                    R"("active":"c"}})",
                                    stack_of(layout, "a"), layout.node(stack_of(layout, "a"))->parent)),
              std::string::npos)
            << body;
    EXPECT_NE(body.find(R"("kind":"split","axis":"horizontal","ratio":0.5,)"), std::string::npos) << body;
    EXPECT_NE(body.find(std::format(R"({{"key":"a","registered":true,"title":"Panel a","closable":false,)"
                                    R"("stack":{},"index":0,"float":0,"visible":false,"os_window":null}})",
                                    stack_of(layout, "a"))),
              std::string::npos)
            << body;
    EXPECT_NE(body.find(R"("key":"c","registered":true,"title":"Panel c","closable":false,)"), std::string::npos);
    EXPECT_NE(body.find(R"("floats":[])"), std::string::npos) << body;
    EXPECT_NE(body.find(R"("layout":)" + json_string(engine::ui::dock_layout_to_text(layout))), std::string::npos)
            << body;
}

TEST_F(CliDock, ListsFloatsAndAPanelTheLayoutLacks) {
    DockLayout layout = two_floats();
    layout.remove("a");
    const engine::ecs::Entity entity = make_space(std::move(layout));
    const DockLayout &placed = space(entity).layout;
    const DockNodeId b_float = placed.find("b")->float_id;

    const std::string body = dock(R"({"command":"dock"})");
    EXPECT_NE(body.find(std::format(R"({{"id":{},"root":{},"rect":{{"x":100,"y":100,"w":300,"h":200}},)"
                                    R"("os_window":null}})",
                                    b_float, stack_of(placed, "b"))),
              std::string::npos)
            << body;
    EXPECT_NE(body.find(std::format(R"("key":"b","registered":true,"title":"Panel b","closable":false,"stack":{},)"
                                    R"("index":0,"float":{},"visible":true)",
                                    stack_of(placed, "b"), b_float)),
              std::string::npos)
            << body;
    EXPECT_NE(body.find(R"({"key":"a","registered":true,"title":"Panel a","closable":false,"stack":null,)"
                        R"("index":null,"float":0,"visible":false,"os_window":null})"),
              std::string::npos)
            << body;
}

TEST_F(CliDock, ActivateSwitchesTheTabAndBumpsTheRevision) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const engine::ecs::Entity a_canvas = panel_canvases[0];
    const engine::ecs::Entity c_canvas = panel_canvases[2];
    EXPECT_EQ(world.get<UiCanvas>(a_canvas).rect.w, 0.0f);
    EXPECT_GT(world.get<UiCanvas>(c_canvas).rect.w, 0.0f);

    const std::string body = dock(R"({"command":"dock","action":"activate","panel":"a"})");
    EXPECT_TRUE(body.starts_with(R"({"ok":true,"result":{"space":0,"action":"activate","changed":true,)"
                                 R"("revision":1,"float_mode":"virtual","layout":)"))
            << body;
    EXPECT_NE(body.find(json_string(engine::ui::dock_layout_to_text(space(entity).layout))), std::string::npos);
    EXPECT_TRUE(space(entity).layout.is_visible("a"));
    EXPECT_EQ(space(entity).revision, 1u);

    // The next layout pass shows it: `a` gets the stack's content, `c` an empty rect.
    frame();
    EXPECT_GT(world.get<UiCanvas>(a_canvas).rect.w, 0.0f);
    EXPECT_EQ(world.get<UiCanvas>(c_canvas).rect.w, 0.0f);

    // Already active: nothing changes, the revision stays.
    EXPECT_NE(dock(R"({"command":"dock","action":"activate","panel":"a"})").find(R"("changed":false,"revision":1)"),
              std::string::npos);
    EXPECT_EQ(space(entity).revision, 1u);
}

TEST_F(CliDock, ActivateRaisesItsVirtualFloat) {
    const engine::ecs::Entity entity = make_space(two_floats());
    const DockNodeId b_float = space(entity).layout.find("b")->float_id;
    ASSERT_NE(space(entity).layout.floats().back().id, b_float);

    EXPECT_NE(dock(R"({"command":"dock","action":"activate","panel":"b"})").find(R"("changed":true,"revision":1)"),
              std::string::npos);
    EXPECT_EQ(space(entity).layout.floats().back().id, b_float);
}

TEST_F(CliDock, MoveGoesThroughTheLayoutAndChecksTheTarget) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    DockLayout expected = space(entity).layout;
    const DockNodeId b_stack = stack_of(expected, "b");
    ASSERT_TRUE(expected.move("a", {b_stack, DockZone::Bottom}));

    const std::string body =
            dock(std::format(R"({{"command":"dock","action":"move","panel":"a","node":{},"zone":"bottom"}})", b_stack));
    EXPECT_NE(body.find(R"("action":"move","changed":true,"revision":1)"), std::string::npos) << body;
    EXPECT_EQ(space(entity).layout, expected);

    // Node 0 is the dock area: an edge splits the docked root.
    ASSERT_TRUE(expected.move("c", {engine::ui::kNoDockNode, DockZone::Left}));
    dock(R"({"command":"dock","action":"move","panel":"c","node":0,"zone":"left"})");
    EXPECT_EQ(space(entity).layout, expected);
    EXPECT_EQ(space(entity).revision, 2u);

    // No change: Center on its own stack.
    EXPECT_NE(dock(std::format(R"({{"command":"dock","action":"move","panel":"b","node":{},"zone":"center"}})",
                               stack_of(expected, "b")))
                      .find(R"("changed":false,"revision":2)"),
              std::string::npos);

    EXPECT_EQ(dock(R"({"command":"dock","action":"move","panel":"a","node":999,"zone":"left"})"),
              R"({"ok":false,"error":"no node 999"})");
    EXPECT_EQ(dock(R"({"command":"dock","action":"move","panel":"a","node":0,"zone":"middle"})"),
              R"({"ok":false,"error":"unknown zone middle"})");
    EXPECT_EQ(dock(R"({"command":"dock","action":"move","panel":"z","node":0,"zone":"left"})"),
              R"({"ok":false,"error":"no panel z"})");
    EXPECT_EQ(dock(R"({"command":"dock","action":"move","node":0})"), R"({"ok":false,"error":"move needs a panel"})");
    EXPECT_EQ(space(entity).layout, expected);
    EXPECT_EQ(space(entity).revision, 2u);
}

TEST_F(CliDock, FloatTakesAFrameOrCentersTheFloatSize) {
    const engine::ecs::Entity entity = make_space(side_by_side());

    dock(R"({"command":"dock","action":"float","panel":"a"})");
    const DockLayout &layout = space(entity).layout;
    ASSERT_EQ(layout.floats().size(), 1u);
    EXPECT_EQ(layout.floats()[0].rect, (Rect{240.0f, 180.0f, 320.0f, 240.0f}));
    EXPECT_EQ(layout.find("a")->float_id, layout.floats()[0].id);
    EXPECT_EQ(space(entity).revision, 1u);

    // A panel alone in its float only gets the new frame.
    dock(R"({"command":"dock","action":"float","panel":"a","x":10,"y":20,"w":200,"h":100})");
    ASSERT_EQ(layout.floats().size(), 1u);
    EXPECT_EQ(layout.floats()[0].rect, (Rect{10.0f, 20.0f, 200.0f, 100.0f}));
    EXPECT_EQ(space(entity).revision, 2u);

    EXPECT_EQ(dock(R"({"command":"dock","action":"float","panel":"b","x":10,"y":20})"),
              R"({"ok":false,"error":"float needs x, y, and a positive w and h"})");
    EXPECT_EQ(dock(R"({"command":"dock","action":"float","panel":"b","x":10,"y":20,"w":0,"h":10})"),
              R"({"ok":false,"error":"float needs x, y, and a positive w and h"})");
    EXPECT_EQ(space(entity).revision, 2u);
}

TEST_F(CliDock, ModeSwitchesTheFloatModeWithoutARevision) {
    const engine::ecs::Entity entity = make_space(side_by_side());

    const std::string body = dock(R"({"command":"dock","action":"mode","mode":"os"})");
    EXPECT_NE(body.find(R"("action":"mode","changed":true,"revision":0,"float_mode":"os")"), std::string::npos)
            << body;
    EXPECT_EQ(space(entity).float_mode, DockFloatMode::OsWindow);
    EXPECT_NE(dock(R"({"command":"dock","action":"mode","mode":"os"})").find(R"("changed":false)"), std::string::npos);
    dock(R"({"command":"dock","action":"mode","mode":"virtual"})");
    EXPECT_EQ(space(entity).float_mode, DockFloatMode::Virtual);

    EXPECT_EQ(dock(R"({"command":"dock","action":"mode","mode":"window"})"),
              R"({"ok":false,"error":"unknown float mode window"})");
    EXPECT_EQ(dock(R"({"command":"dock","action":"spin"})"), R"({"ok":false,"error":"unknown dock action spin"})");
}

TEST_F(CliDock, SpacesArePickedByPanelOrPlace) {
    EXPECT_EQ(dock(R"({"command":"dock"})"), R"({"ok":false,"error":"no dock space on window 0"})");

    // Placed by order: the space made second has the lower order, so it is place 0.
    const engine::ecs::Entity upper = make_space(side_by_side(), 40);
    DockLayout other;
    other.add("a", {});
    const engine::ecs::Entity lower = make_space(std::move(other), 10);

    EXPECT_EQ(dock(R"({"command":"dock","action":"activate","panel":"a"})"),
              R"({"ok":false,"error":"ambiguous","candidates":[{"space":0,"window":0,"order":10},)"
              R"({"space":1,"window":0,"order":40}]})");
    EXPECT_EQ(dock(R"({"command":"dock","action":"mode","mode":"os"})").find(R"("error":"ambiguous")"), 12u);

    // Only one space holds `b`.
    EXPECT_NE(dock(R"({"command":"dock","action":"activate","panel":"b"})").find(R"("space":1,)"), std::string::npos);
    EXPECT_EQ(space(upper).revision, 0u);

    EXPECT_NE(dock(R"({"command":"dock","action":"activate","panel":"a","space":1})").find(R"("changed":true)"),
              std::string::npos);
    EXPECT_TRUE(space(upper).layout.is_visible("a"));
    EXPECT_EQ(space(lower).revision, 0u);

    const std::string listed = dock(R"({"command":"dock","space":0})");
    EXPECT_NE(listed.find(R"("spaces":[{"space":0,"window":0,"order":10,)"), std::string::npos) << listed;
    EXPECT_EQ(listed.find(R"("order":40)"), std::string::npos) << listed;
    EXPECT_EQ(dock(R"({"command":"dock","space":2})"), R"({"ok":false,"error":"no dock space 2 on window 0"})");
}

TEST_F(CliDock, RefusedWhileAGestureIsInProgress) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    world.ctx<engine::ui::DockRuntime>().spaces[entity].gesture.kind = engine::ui::DockGestureKind::TabDrag;

    EXPECT_NE(dock(R"({"command":"dock"})").find(R"("gesture":true)"), std::string::npos);
    EXPECT_EQ(dock(R"({"command":"dock","action":"activate","panel":"a"})"),
              R"({"ok":false,"error":"a dock gesture is in progress"})");
    EXPECT_FALSE(space(entity).layout.is_visible("a"));
    EXPECT_EQ(space(entity).revision, 0u);
}

#if defined(ENGINE_CLI_SERVER)

TEST_F(CliDockLoopback, DockIsAnsweredFromTheWindowsWorld) {
    const cli_client::Descriptor descriptor = cli_client::read_descriptor();
    ASSERT_GT(descriptor.port, 0);
    const engine::ecs::Entity entity = make_space(side_by_side());
    const engine::cli::CliFrame frame{.world_for = [this](engine::WindowId window) -> engine::ecs::World * {
        return window == kPrimaryWindow ? &world : nullptr;
    }};

    cli_client::Reply reply;
    std::thread client([&] {
        reply = cli_client::post_authorized(descriptor, R"({"command":"dock","action":"activate","panel":"a"})");
    });
    engine::cli::wait_for_request();
    engine::cli::begin_frame(frame);
    EXPECT_FALSE(space(entity).layout.is_visible("a"));
    engine::cli::drain(frame);
    client.join();
    EXPECT_EQ(reply.status, 200);
    EXPECT_NE(reply.body.find(R"("changed":true,"revision":1)"), std::string::npos) << reply.body;
    EXPECT_TRUE(space(entity).layout.is_visible("a"));

    std::thread missing([&] {
        reply = cli_client::post_authorized(descriptor, R"({"command":"dock","window":3})");
    });
    engine::cli::wait_for_request();
    engine::cli::drain(frame);
    missing.join();
    EXPECT_EQ(reply.body, R"({"ok":false,"error":"no world on window 3"})");
}

#endif
