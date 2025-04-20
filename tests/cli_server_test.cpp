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

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#if defined(ENGINE_UI_PROFILER)
#include "ui/profile.h"

#include <engine/ui/profiler.h>
#endif

#if defined(ENGINE_CLI_SERVER)
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#endif

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

#if defined(ENGINE_CLI_SERVER)

    struct Reply {
        int status = 0;
        std::string body;
    };

    std::uint32_t this_pid() {
#if defined(_WIN32)
        return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
        return static_cast<std::uint32_t>(::getpid());
#endif
    }

    std::string read_file(const std::filesystem::path &path) {
        std::ifstream file(path);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    Reply post(int port, std::string_view extra, std::string_view body) {
        Reply reply;
#if defined(_WIN32)
        using Socket = SOCKET;
        constexpr Socket invalid = INVALID_SOCKET;
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return reply;
        }
#else
        using Socket = int;
        constexpr Socket invalid = -1;
#endif
        const Socket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == invalid) {
#if defined(_WIN32)
            WSACleanup();
#endif
            return reply;
        }
#if defined(_WIN32)
        const DWORD timeout = 3000;
        ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
        timeval timeout{};
        timeout.tv_sec = 3;
        ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<unsigned short>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
#if defined(_WIN32)
            closesocket(socket);
            WSACleanup();
#else
            ::close(socket);
#endif
            return reply;
        }
        const std::string request =
                std::format("POST /exec HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                            "Content-Length: {}\r\n{}Connection: close\r\n\r\n{}",
                            body.size(), extra, body);
        std::string_view pending = request;
        while (!pending.empty()) {
            const int sent = ::send(socket, pending.data(), static_cast<int>(pending.size()), 0);
            if (sent <= 0) {
#if defined(_WIN32)
                closesocket(socket);
                WSACleanup();
#else
                ::close(socket);
#endif
                return reply;
            }
            pending.remove_prefix(static_cast<std::size_t>(sent));
        }
        std::string data;
        char buffer[2048];
        while (true) {
            const int got = ::recv(socket, buffer, sizeof(buffer), 0);
            if (got < 0) {
                break;
            }
            if (got == 0) {
                break;
            }
            data.append(buffer, static_cast<std::size_t>(got));
        }
#if defined(_WIN32)
        closesocket(socket);
        WSACleanup();
#else
        ::close(socket);
#endif
        const std::size_t line = data.find("\r\n");
        if (line == std::string::npos || !data.starts_with("HTTP/1.1 ")) {
            return reply;
        }
        reply.status = std::atoi(data.c_str() + 9);
        const std::size_t split = data.find("\r\n\r\n");
        if (split != std::string::npos) {
            reply.body = data.substr(split + 4);
        }
        return reply;
    }

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

TEST(Cli, ProfileMissingFromThisBuild) {
    engine::ecs::World world;
    const std::string json = run(world, "profile").json;
    EXPECT_NE(json.find("UI profiler is not in this build"), std::string::npos);
}

#endif

#if defined(ENGINE_CLI_SERVER)

TEST_F(CliLoopback, RejectsMissingTokenAndOrigin) {
    const std::filesystem::path file = engine::cli::descriptor_directory() / (std::to_string(this_pid()) + ".json");
    const std::string descriptor = read_file(file);
    ASSERT_NE(descriptor.find("\"port\":"), std::string::npos);
    const int port = std::atoi(descriptor.c_str() + descriptor.find("\"port\":") + 7);
    ASSERT_GT(port, 0);
    const auto token_at = descriptor.find("\"token\":\"");
    ASSERT_NE(token_at, std::string::npos);
    const std::size_t token_begin = token_at + std::string("\"token\":\"").size();
    const std::size_t token_end = descriptor.find('"', token_begin);
    ASSERT_NE(token_end, std::string::npos);
    const std::string token = descriptor.substr(token_begin, token_end - token_begin);

    const Reply denied = post(port, "", "{\"command\":\"tree\"}");
    EXPECT_EQ(denied.status, 401);
    EXPECT_NE(denied.body.find("unauthorized"), std::string::npos);

    const std::string origin = std::format("Origin: http://evil\r\nAuthorization: Bearer {}\r\n", token);
    const Reply rejected = post(port, origin, "{\"command\":\"tree\"}");
    EXPECT_EQ(rejected.status, 403);
    EXPECT_NE(rejected.body.find("origin"), std::string::npos);

    GameCanvas game = spawn_game();
    Reply accepted;
    std::thread client([&] {
        accepted = post(port, std::format("Authorization: Bearer {}\r\n", token), "{\"command\":\"tree\"}");
    });
    engine::cli::wait_for_request();
    engine::cli::drain(game.world);
    client.join();
    EXPECT_EQ(accepted.status, 200);
    EXPECT_NE(accepted.body.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(accepted.body.find("\"id\":\"go\""), std::string::npos);
}

#endif
