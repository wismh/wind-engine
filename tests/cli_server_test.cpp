#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <gtest/gtest.h>

#include "cli/cli_server.h"
#include "ui/painter.h"

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

#if defined(ENGINE_UI_PROFILER)
#include "ui/profile.h"

#include <engine/ui/profiler.h>
#endif

#include "fixtures/cli_client.h"

#if defined(NANOVG_H) || defined(NANOVG_GL_H) || defined(NANOVG_GL3)
#error "cli tests must not include nvg headers"
#endif

namespace {

    class ClickViewModel final : public engine::ui::ViewModel {
    public:
        int clicks = 0;
        engine::ui::RelayCommand click;

        ClickViewModel() {
            command(engine::ui::intern("click"), click);
            click = [this] { ++clicks; };
        }
    };

    class NullPainter final : public engine::ui::IUiPainter {
    public:
        void save() override {}
        void restore() override {}
        void scissor(const engine::render::Rect &) override {}
        void apply_transform(glm::vec2, float, float) override {}
        void apply_view(glm::vec2, glm::vec2, float) override {}
        void set_opacity(float) override {}
        void fill_rounded_rect(const engine::render::Rect &, float, glm::vec4) override {}
        void fill_rounded_rect_gradient(const engine::render::Rect &, float, const engine::ui::Gradient &) override {}
        void stroke_rounded_rect(const engine::render::Rect &, float, float, glm::vec4) override {}
        void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
        void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
        void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override {}
        void set_font(engine::AssetId, float) override {}
        void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {}
        void image(engine::AssetId, const engine::render::Rect &) override {}
        void image_repeat(engine::AssetId, const engine::render::Rect &) override {}
        void image_nine_slice(engine::AssetId, const engine::render::Rect &, const engine::ui::BoxInsets &) override {}
        glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
            return {static_cast<float>(text.size()) * size * 0.5f, size};
        }
    };

    struct GameCanvas {
        engine::ecs::World world;
        std::shared_ptr<ClickViewModel> vm = std::make_shared<ClickViewModel>();
        engine::ecs::Entity entity{};
    };

    GameCanvas spawn_game() {
        GameCanvas game;
        engine::ui::presentation_of(game.world).sizes.sizes[engine::kPrimaryWindow] = {800, 600};
        const auto parsed = engine::ui::parse_xml(
                R"(<Canvas><Button id="go" command="{binding click}"><Label id="lab" text="Go"/></Button></Canvas>)");
        EXPECT_TRUE(parsed.has_value());
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css("Button { width: 100px; height: 40px; margin: 0; padding: 0; }\n"
                                           "Label { width: 80px; height: 20px; margin: 4px; color: #ffffff; }\n"
                                           "#go > Label { color: #ff0000; }\n"
                                           "@media (min-width: 4000px) { Label { color: #00ff00; } }\n"
                                           "Label:hover { color: #0000ff; }\n",
                                           warnings);
        EXPECT_TRUE(sheet.has_value());
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.order = 0;
        canvas.rect = {0.0f, 0.0f, 200.0f, 100.0f};
        canvas.data_context = game.vm;
        if (parsed.has_value() && sheet.has_value()) {
            game.entity = engine::ui::spawn_canvas(game.world, canvas, *parsed, std::move(*sheet));
        }
        return game;
    }

    void layout_instance(engine::ecs::World &world, engine::ecs::Entity entity) {
        engine::ui::UiInstance &instance = world.get<engine::ui::UiInstance>(entity);
        engine::ui::UiCanvas &canvas = world.get<engine::ui::UiCanvas>(entity);
        if (canvas.data_context) {
            ASSERT_TRUE(engine::ui::apply_bindings(instance.document, *canvas.data_context).has_value());
        }
        const engine::ui::Stylesheet *sheet = instance.stylesheet ? &*instance.stylesheet : nullptr;
        const engine::ui::WindowSize size = engine::ui::window_size_for(world, canvas.window);
        engine::ui::apply_layout_style(instance.document.root, sheet, static_cast<float>(size.width),
                                       static_cast<float>(size.height));
        engine::ui::layout(instance.document, canvas.rect);
    }

    engine::ui::Element *find_id(engine::ui::Element &element, std::string_view id) {
        if (element.id == id) {
            return &element;
        }
        for (engine::ui::Element &child: element.children) {
            if (engine::ui::Element *found = find_id(child, id)) {
                return found;
            }
        }
        return nullptr;
    }

    engine::cli::CliResponse run(engine::ecs::World &world, std::string command, std::string selector = {},
                                 bool stop = false) {
        engine::cli::CliRequest request;
        request.command = std::move(command);
        request.selector = std::move(selector);
        request.stop = stop;
        return engine::cli::execute(world, request);
    }

    engine::cli::CliFrame frame_of(engine::ecs::World &world) {
        return engine::cli::CliFrame{.world_for = [&world](engine::WindowId window) -> engine::ecs::World * {
            return window == engine::kPrimaryWindow ? &world : nullptr;
        }};
    }

#if defined(ENGINE_CLI_SERVER)

    class CliLoopback : public ::testing::Test {
    protected:
        void SetUp() override { engine::cli::start(); }
        void TearDown() override { engine::cli::stop(); }
    };

#endif

} // namespace

TEST(Cli, TreeElementAndWinner) {
    GameCanvas game = spawn_game();
    ASSERT_TRUE(game.world.valid(game.entity));
    layout_instance(game.world, game.entity);
    NullPainter painter;
    engine::ui::UiInstance &instance = game.world.get<engine::ui::UiInstance>(game.entity);
    engine::ui::paint_document(instance.document, instance.stylesheet ? &*instance.stylesheet : nullptr, painter,
                               engine::ui::UiPaintInput{
                                       .canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f},
                                       .window_width = 800.0f,
                                       .window_height = 600.0f,
                                       .canvas = game.entity,
                               });

    const std::string tree = run(game.world, "tree").json;
    EXPECT_NE(tree.find("\"id\":\"go\""), std::string::npos);
    EXPECT_NE(tree.find("\"kind\":\"Button\""), std::string::npos);
    EXPECT_NE(tree.find("\"id\":\"lab\""), std::string::npos);

    const std::string element = run(game.world, "element", "#lab").json;
    const auto rule = element.find("\"selector\":\"#go > Label\"");
    ASSERT_NE(rule, std::string::npos);
    const auto winner = element.find("\"winner\":", rule);
    ASSERT_NE(winner, std::string::npos);
    EXPECT_EQ(element.compare(winner, std::string("\"winner\":true").size(), "\"winner\":true"), 0);
    EXPECT_EQ(element.find("Label:hover"), std::string::npos);
    EXPECT_NE(element.find("\"color\":[1,0,0,1]"), std::string::npos);
    EXPECT_NE(element.find("\"text\":\"Go\""), std::string::npos) << element;

    engine::ecs::World twins;
    const auto twin_xml = engine::ui::parse_xml(
            R"(<Canvas><Button id="a" class="twin"/><Button id="b" class="twin"/></Canvas>)");
    ASSERT_TRUE(twin_xml.has_value());
    engine::ui::UiCanvas twin_canvas;
    twin_canvas.fit = engine::ui::UiFit::Fixed;
    twin_canvas.rect = {0.0f, 0.0f, 80.0f, 40.0f};
    ASSERT_TRUE(twins.valid(engine::ui::spawn_canvas(twins, twin_canvas, *twin_xml)));
    const std::string ambiguous = run(twins, "element", ".twin").json;
    EXPECT_NE(ambiguous.find("\"error\":\"ambiguous\""), std::string::npos);
    EXPECT_NE(ambiguous.find("\"id\":\"a\""), std::string::npos);
    EXPECT_NE(ambiguous.find("\"id\":\"b\""), std::string::npos);
}

TEST(Cli, HitAndClickBypassPick) {
    GameCanvas game = spawn_game();
    ASSERT_TRUE(game.world.valid(game.entity));
    layout_instance(game.world, game.entity);
    engine::ui::set_inspector_attached(game.world, true);
    game.world.ctx<engine::ui::UiInspector>().pick_pointer = true;

    engine::ui::Element *button =
            find_id(game.world.get<engine::ui::UiInstance>(game.entity).document.root, "go");
    ASSERT_NE(button, nullptr);

    engine::cli::CliRequest hit;
    hit.command = "hit";
    hit.x = button->layout_rect.x + 1.0f;
    hit.y = button->layout_rect.y + 1.0f;
    hit.has_x = true;
    hit.has_y = true;
    const std::string hit_json = engine::cli::execute(game.world, hit).json;
    EXPECT_NE(hit_json.find("\"id\":\"go\""), std::string::npos) << hit_json;
    EXPECT_FALSE(engine::ui::presentation_of(game.world).mouse.consumed_for());

    engine::cli::CliRequest miss;
    miss.command = "hit";
    miss.x = -10.0;
    miss.y = -10.0;
    miss.has_x = true;
    miss.has_y = true;
    EXPECT_NE(engine::cli::execute(game.world, miss).json.find("\"result\":null"), std::string::npos);

    const std::string clicked = run(game.world, "click", "#go").json;
    EXPECT_NE(clicked.find("\"executed\":true"), std::string::npos);
    EXPECT_EQ(game.vm->clicks, 1);
    EXPECT_FALSE(engine::ui::inspector_selection(game.world).active);
    EXPECT_FALSE(engine::ui::presentation_of(game.world).mouse.consumed_for());

    const std::string label = run(game.world, "click", "#lab").json;
    EXPECT_NE(label.find("\"reason\":\"no command\""), std::string::npos);
    EXPECT_EQ(game.vm->clicks, 1);

    button->disabled = true;
    const std::string disabled = run(game.world, "click", "#go").json;
    EXPECT_NE(disabled.find("\"reason\":\"disabled\""), std::string::npos);
    EXPECT_EQ(game.vm->clicks, 1);

    button->disabled = false;
    game.vm->click.set_can_execute(false);
    const std::string blocked = run(game.world, "click", "#go").json;
    EXPECT_NE(blocked.find("\"reason\":\"can_execute\""), std::string::npos);
    EXPECT_EQ(game.vm->clicks, 1);
}

TEST(Cli, HostRepliesAreWrittenAsJson) {
    engine::cli::CliRequest request;
    request.command = "state";
    EXPECT_EQ(engine::cli::execute_host(nullptr, request), R"({"ok":false,"error":"unknown command"})");
    EXPECT_FALSE(engine::cli::is_ui_command("state"));
    EXPECT_TRUE(engine::cli::is_ui_command("click"));

    const engine::CliCommands host{
            .kind = "editor",
            .handle = [](const engine::CliCommand &command) -> std::optional<engine::CliReply> {
                if (command.name == "fail") {
                    return engine::CliReply{.ok = false, .error = R"(not "playable")", .result = {}};
                }
                if (command.name != "state") {
                    return std::nullopt;
                }
                return engine::CliReply{.ok = true,
                                        .error = {},
                                        .result = {
                                                {"project", std::monostate{}},
                                                {"playable", true},
                                                {"frames", std::int64_t{1234567890123}},
                                                {"ratio", 0.5},
                                                {"status", std::string("C:\\games\n\"ttt\"")},
                                        }};
            },
    };
    EXPECT_EQ(engine::cli::execute_host(&host, request),
              R"({"ok":true,"result":{"project":null,"playable":true,"frames":1234567890123,"ratio":0.5,)"
              R"("status":"C:\\games\n\"ttt\""}})");
    request.command = "fail";
    EXPECT_EQ(engine::cli::execute_host(&host, request), R"({"ok":false,"error":"not \"playable\""})");
    request.command = "fly";
    EXPECT_EQ(engine::cli::execute_host(&host, request), R"({"ok":false,"error":"unknown command"})");

    const engine::cli::CliRequest parsed = engine::cli::parse_request(R"({"command":"open","path":"C:/g"})");
    EXPECT_TRUE(parsed.error.empty());
    EXPECT_EQ(parsed.command, "open");
    EXPECT_EQ(parsed.path, "C:/g");
}

#if defined(ENGINE_UI_PROFILER)

TEST(Cli, ProfileCaptureWithoutWindow) {
    engine::ecs::World world;
    struct Stop {
        engine::ecs::World &world;
        ~Stop() { (void) run(world, "profile", {}, true); }
    } stop{world};

    const engine::cli::CliResponse pending = run(world, "profile");
    EXPECT_TRUE(pending.pending);
    EXPECT_TRUE(pending.json.empty());

    engine::ui::begin_frame(world);
    engine::ui::begin_frame(world);
    const std::string ready = run(world, "profile").json;
    EXPECT_NE(ready.find("\"begin_frame\""), std::string::npos);
    EXPECT_NE(ready.find("\"capturing\":true"), std::string::npos);
    EXPECT_NE(ready.find("\"paused\":false"), std::string::npos);
    EXPECT_FALSE(engine::ui::ui_profiler_attached(world));

    const std::string stopped = run(world, "profile", {}, true).json;
    EXPECT_NE(stopped.find("\"capturing\":false"), std::string::npos);
    engine::ui::begin_frame(world);
    EXPECT_TRUE(engine::ui::profiler_shared_frames(world).empty());
}

#else

// Without ENGINE_UI_PROFILER: an exported game's Release. The editor build always has it.
TEST(Cli, ProfileMissingFromThisBuild) {
    engine::ecs::World world;
    const std::string json = run(world, "profile").json;
    EXPECT_NE(json.find("UI profiler is not in this build"), std::string::npos);
}

#endif

#if defined(ENGINE_CLI_SERVER)

TEST_F(CliLoopback, RejectsMissingTokenAndOrigin) {
    const cli_client::Descriptor descriptor = cli_client::read_descriptor();
    ASSERT_GT(descriptor.port, 0);
    ASSERT_FALSE(descriptor.token.empty());
    EXPECT_EQ(descriptor.kind, "game");

    const cli_client::Reply denied = cli_client::post(descriptor.port, "", "{\"command\":\"tree\"}");
    EXPECT_EQ(denied.status, 401);
    EXPECT_NE(denied.body.find("unauthorized"), std::string::npos);

    const std::string origin = std::format("Origin: http://evil\r\nAuthorization: Bearer {}\r\n", descriptor.token);
    const cli_client::Reply rejected = cli_client::post(descriptor.port, origin, "{\"command\":\"tree\"}");
    EXPECT_EQ(rejected.status, 403);
    EXPECT_NE(rejected.body.find("origin"), std::string::npos);

    GameCanvas game = spawn_game();
    cli_client::Reply accepted;
    std::thread client([&] { accepted = cli_client::post_authorized(descriptor, "{\"command\":\"tree\"}"); });
    engine::cli::wait_for_request();
    engine::cli::drain(frame_of(game.world));
    client.join();
    EXPECT_EQ(accepted.status, 200);
    EXPECT_NE(accepted.body.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(accepted.body.find("\"id\":\"go\""), std::string::npos);
}

TEST_F(CliLoopback, WindowWithoutAWorldAnswersAtOnce) {
    const cli_client::Descriptor descriptor = cli_client::read_descriptor();
    ASSERT_GT(descriptor.port, 0);

    // The editor between plays: kPrimaryWindow has no world. One drain answers; nothing waits for the 504.
    cli_client::Reply reply;
    std::thread client([&] { reply = cli_client::post_authorized(descriptor, "{\"command\":\"tree\"}"); });
    engine::cli::wait_for_request();
    engine::cli::drain(engine::cli::CliFrame{.world_for = [](engine::WindowId) -> engine::ecs::World * {
        return nullptr;
    }});
    client.join();
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(reply.body, "{\"ok\":false,\"error\":\"no world on window 0\"}");
}

TEST_F(CliLoopback, EachWindowReachesItsOwnWorld) {
    const cli_client::Descriptor descriptor = cli_client::read_descriptor();
    ASSERT_GT(descriptor.port, 0);

    GameCanvas game = spawn_game();
    engine::ecs::World other;
    const engine::WindowId second{1};
    const auto xml = engine::ui::parse_xml(R"(<Canvas><Button id="tool"/></Canvas>)");
    ASSERT_TRUE(xml.has_value());
    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = {0.0f, 0.0f, 80.0f, 40.0f};
    canvas.window = second;
    ASSERT_TRUE(other.valid(engine::ui::spawn_canvas(other, canvas, *xml)));
    const engine::cli::CliFrame frame{
            .world_for = [&](engine::WindowId window) -> engine::ecs::World * {
                if (window == engine::kPrimaryWindow) {
                    return &game.world;
                }
                return window == second ? &other : nullptr;
            },
    };

    cli_client::Reply reply;
    std::thread client(
            [&] { reply = cli_client::post_authorized(descriptor, "{\"command\":\"tree\",\"window\":1}"); });
    engine::cli::wait_for_request();
    engine::cli::drain(frame);
    client.join();
    EXPECT_NE(reply.body.find("\"id\":\"tool\""), std::string::npos) << reply.body;
    EXPECT_EQ(reply.body.find("\"id\":\"go\""), std::string::npos) << reply.body;
}

TEST_F(CliLoopback, OtherCommandsGoToTheHostAfterTheFrameDrew) {
    const cli_client::Descriptor descriptor = cli_client::read_descriptor();
    ASSERT_GT(descriptor.port, 0);

    std::string seen_path;
    const engine::CliCommands host{
            .kind = "editor",
            .handle = [&](const engine::CliCommand &command) -> std::optional<engine::CliReply> {
                if (command.name != "open") {
                    return std::nullopt;
                }
                seen_path = command.path;
                return engine::CliReply{.ok = true, .error = {}, .result = {{"requested", std::string("open")}}};
            },
    };
    const engine::cli::CliFrame frame{
            .world_for = [](engine::WindowId) -> engine::ecs::World * { return nullptr; },
            .host = &host,
    };

    cli_client::Reply opened;
    std::thread client([&] {
        opened = cli_client::post_authorized(descriptor, "{\"command\":\"open\",\"path\":\"C:/games/ttt\"}");
    });
    engine::cli::wait_for_request();
    engine::cli::begin_frame(frame);
    EXPECT_TRUE(seen_path.empty());
    engine::cli::drain(frame);
    client.join();
    EXPECT_EQ(seen_path, "C:/games/ttt");
    EXPECT_EQ(opened.body, "{\"ok\":true,\"result\":{\"requested\":\"open\"}}");

    cli_client::Reply unknown;
    std::thread second([&] { unknown = cli_client::post_authorized(descriptor, "{\"command\":\"fly\"}"); });
    engine::cli::wait_for_request();
    engine::cli::drain(frame);
    second.join();
    EXPECT_EQ(unknown.body, "{\"ok\":false,\"error\":\"unknown command\"}");
}

#endif
