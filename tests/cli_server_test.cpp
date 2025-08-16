#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <gtest/gtest.h>

#include "cli/cli_server.h"
#include "cli/screenshot.h"
#include "resources/importers.h"
#include "ui/painter.h"

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

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

    struct ScratchDir {
        std::filesystem::path path;

        ScratchDir() {
            static int seq = 0;
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() /
                   ("wind_cli_screenshot_" + std::to_string(stamp) + "_" + std::to_string(++seq));
            std::filesystem::create_directories(path);
        }

        ~ScratchDir() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        ScratchDir(const ScratchDir &) = delete;
        ScratchDir &operator=(const ScratchDir &) = delete;

        [[nodiscard]] std::string file(std::string_view name) const {
            const std::u8string text = (path / name).u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }
    };

    // A Windows path inside a JSON string: each backslash doubled.
    std::string json_path(std::string_view path) {
        std::string escaped;
        for (const char c: path) {
            escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
        }
        return escaped;
    }

    // Pixel (x, y) is (x, y, 7, 255), so a crop's first pixel names its origin.
    engine::render::TextureDesc coordinate_image(int width, int height) {
        engine::render::TextureDesc image;
        image.width = width;
        image.height = height;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                image.rgba.push_back(static_cast<std::uint8_t>(x));
                image.rgba.push_back(static_cast<std::uint8_t>(y));
                image.rgba.push_back(7);
                image.rgba.push_back(255);
            }
        }
        return image;
    }

    std::optional<engine::render::TextureDesc> read_png(const std::string &path) {
        std::ifstream file(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(path.data()),
                                                               path.size())),
                           std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return engine::decode_png_rgba(bytes);
    }

    engine::cli::CliRequest screenshot_request(std::string path, std::string selector = {}) {
        engine::cli::CliRequest request;
        request.command = "screenshot";
        request.path = std::move(path);
        request.selector = std::move(selector);
        return request;
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

namespace {

    // Two canvases on one window that reuse the id `tree`, like the editor's Explorer and Inspector panels.
    struct TwoPanels {
        engine::ecs::World world;
        std::shared_ptr<ClickViewModel> explorer_vm = std::make_shared<ClickViewModel>();
        std::shared_ptr<ClickViewModel> inspector_vm = std::make_shared<ClickViewModel>();
        engine::ecs::Entity explorer{};
        engine::ecs::Entity inspector{};
    };

    engine::ecs::Entity spawn_panel(engine::ecs::World &world, std::string_view root_id, float x,
                                    std::shared_ptr<ClickViewModel> vm) {
        const auto parsed = engine::ui::parse_xml(
                std::format(R"(<Canvas id="{}"><Button id="tree" command="{{binding click}}"/></Canvas>)", root_id));
        EXPECT_TRUE(parsed.has_value());
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css("Button { width: 60px; height: 30px; margin: 5px; padding: 0; }", warnings);
        EXPECT_TRUE(sheet.has_value());
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.rect = {x, 0.0f, 200.0f, 100.0f};
        canvas.data_context = std::move(vm);
        if (!parsed.has_value() || !sheet.has_value()) {
            return {};
        }
        const engine::ecs::Entity entity = engine::ui::spawn_canvas(world, canvas, *parsed, std::move(*sheet));
        layout_instance(world, entity);
        return entity;
    }

    TwoPanels spawn_two_panels() {
        TwoPanels panels;
        engine::ui::presentation_of(panels.world).sizes.sizes[engine::kPrimaryWindow] = {400, 100};
        panels.explorer = spawn_panel(panels.world, "explorer", 0.0f, panels.explorer_vm);
        panels.inspector = spawn_panel(panels.world, "inspector", 200.0f, panels.inspector_vm);
        return panels;
    }

    engine::cli::CliResponse run_on(engine::ecs::World &world, std::string command, std::string selector,
                                    std::variant<std::monostate, std::uint32_t, std::string> canvas) {
        engine::cli::CliRequest request;
        request.command = std::move(command);
        request.selector = std::move(selector);
        request.canvas = std::move(canvas);
        return engine::cli::execute(world, request);
    }

} // namespace

TEST(Cli, ParsesTheCanvasAsAPlaceOrARootId) {
    const engine::cli::CliRequest place = engine::cli::parse_request(R"({"command":"tree","canvas":1})");
    EXPECT_TRUE(place.error.empty());
    EXPECT_EQ(place.canvas, (std::variant<std::monostate, std::uint32_t, std::string>{std::uint32_t{1}}));

    const engine::cli::CliRequest root = engine::cli::parse_request(R"({"command":"tree","canvas":"inspector"})");
    EXPECT_TRUE(root.error.empty());
    EXPECT_EQ(root.canvas, (std::variant<std::monostate, std::uint32_t, std::string>{std::string("inspector")}));

    EXPECT_TRUE(std::holds_alternative<std::monostate>(engine::cli::parse_request(R"({"command":"tree"})").canvas));
    for (const char *bad: {R"({"command":"tree","canvas":-1})", R"({"command":"tree","canvas":1.5})",
                           R"({"command":"tree","canvas":""})", R"({"command":"tree","canvas":true})"}) {
        EXPECT_EQ(engine::cli::parse_request(bad).error, "invalid request") << bad;
    }
}

TEST(Cli, CanvasPicksOneOfTwoCanvasesThatShareAnId) {
    TwoPanels panels = spawn_two_panels();
    ASSERT_TRUE(panels.world.valid(panels.explorer));
    ASSERT_TRUE(panels.world.valid(panels.inspector));

    const std::string tree = run_on(panels.world, "tree", {}, {}).json;
    EXPECT_NE(tree.find(R"("canvas":0,"canvas_id":"explorer","path":[0],"kind":"Button","id":"tree")"),
              std::string::npos)
            << tree;
    EXPECT_NE(tree.find(R"("canvas":1,"canvas_id":"inspector","path":[0],"kind":"Button","id":"tree")"),
              std::string::npos)
            << tree;
    const std::string one_tree = run_on(panels.world, "tree", {}, std::uint32_t{1}).json;
    EXPECT_EQ(one_tree.find("\"explorer\""), std::string::npos) << one_tree;
    EXPECT_NE(one_tree.find("\"canvas_id\":\"inspector\""), std::string::npos) << one_tree;

    const std::string ambiguous = run_on(panels.world, "element", "#tree", {}).json;
    EXPECT_NE(ambiguous.find("\"error\":\"ambiguous\""), std::string::npos) << ambiguous;
    EXPECT_NE(ambiguous.find(R"({"window":0,"canvas":0,"canvas_id":"explorer","path":[0])"), std::string::npos)
            << ambiguous;
    EXPECT_NE(ambiguous.find(R"({"window":0,"canvas":1,"canvas_id":"inspector","path":[0])"), std::string::npos)
            << ambiguous;
    EXPECT_NE(run_on(panels.world, "element", "path:0", {}).json.find("\"error\":\"ambiguous\""), std::string::npos);

    const std::string by_place = run_on(panels.world, "element", "#tree", std::uint32_t{1}).json;
    EXPECT_NE(by_place.find(R"("ok":true,"result":{"window":0,"canvas":1,"canvas_id":"inspector")"),
              std::string::npos)
            << by_place;
    EXPECT_NE(by_place.find(R"("border":{"x":205,"y":5,"w":60,"h":30})"), std::string::npos) << by_place;
    const std::string by_root = run_on(panels.world, "element", "path:0", std::string("explorer")).json;
    EXPECT_NE(by_root.find(R"("ok":true,"result":{"window":0,"canvas":0,"canvas_id":"explorer","path":[0])"),
              std::string::npos)
            << by_root;

    const std::string clicked = run_on(panels.world, "click", "#tree", std::string("inspector")).json;
    EXPECT_NE(clicked.find("\"executed\":true"), std::string::npos) << clicked;
    EXPECT_EQ(panels.inspector_vm->clicks, 1);
    EXPECT_EQ(panels.explorer_vm->clicks, 0);
    EXPECT_NE(run_on(panels.world, "click", "#tree", std::uint32_t{0}).json.find("\"executed\":true"),
              std::string::npos);
    EXPECT_EQ(panels.explorer_vm->clicks, 1);
    EXPECT_EQ(panels.inspector_vm->clicks, 1);

    EXPECT_EQ(run_on(panels.world, "element", "#tree", std::uint32_t{2}).json,
              R"({"ok":false,"error":"no canvas 2 on window 0"})");
    EXPECT_EQ(run_on(panels.world, "tree", {}, std::string("nope")).json,
              R"({"ok":false,"error":"no canvas \"nope\" on window 0"})");

    engine::cli::CliRequest hit;
    hit.command = "hit";
    hit.x = 210.0;
    hit.y = 10.0;
    hit.has_x = true;
    hit.has_y = true;
    EXPECT_NE(engine::cli::execute(panels.world, hit).json.find("\"canvas_id\":\"inspector\""), std::string::npos);
    hit.canvas = std::string("explorer");
    EXPECT_NE(engine::cli::execute(panels.world, hit).json.find("\"result\":null"), std::string::npos);
}

TEST(Cli, ScreenshotOfOneCanvasOrOfAnElementOnIt) {
    TwoPanels panels = spawn_two_panels();
    ScratchDir dir;
    const engine::render::TextureDesc image = coordinate_image(400, 100);

    const std::string ambiguous =
            engine::cli::screenshot_json(&panels.world, screenshot_request(dir.file("a.png"), "#tree"), image);
    EXPECT_NE(ambiguous.find("\"error\":\"ambiguous\""), std::string::npos) << ambiguous;

    engine::cli::CliRequest element = screenshot_request(dir.file("tree.png"), "#tree");
    element.canvas = std::string("inspector");
    const std::string element_json = engine::cli::screenshot_json(&panels.world, element, image);
    EXPECT_NE(element_json.find(R"("rect":{"x":205,"y":5,"w":60,"h":30})"), std::string::npos) << element_json;
    const std::optional<engine::render::TextureDesc> tree_png = read_png(element.path);
    ASSERT_TRUE(tree_png.has_value());
    EXPECT_EQ(tree_png->width, 60);
    EXPECT_EQ(tree_png->rgba[0], 205);
    EXPECT_EQ(tree_png->rgba[1], 5);

    engine::cli::CliRequest whole = screenshot_request(dir.file("panel.png"));
    whole.canvas = std::uint32_t{1};
    const std::string whole_json = engine::cli::screenshot_json(&panels.world, whole, image);
    EXPECT_NE(whole_json.find(R"("rect":{"x":200,"y":0,"w":200,"h":100})"), std::string::npos) << whole_json;
    const std::optional<engine::render::TextureDesc> panel_png = read_png(whole.path);
    ASSERT_TRUE(panel_png.has_value());
    EXPECT_EQ(panel_png->width, 200);
    EXPECT_EQ(panel_png->rgba[0], 200);

    engine::cli::CliRequest missing = screenshot_request(dir.file("none.png"));
    missing.canvas = std::uint32_t{7};
    EXPECT_EQ(engine::cli::screenshot_json(&panels.world, missing, image),
              R"({"ok":false,"error":"no canvas 7 on window 0"})");
    EXPECT_EQ(engine::cli::screenshot_json(nullptr, missing, image), R"({"ok":false,"error":"no world on window 0"})");
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

TEST(Cli, SnapToPixelsGrowsOutwardAndClips) {
    const engine::render::Rect inside = engine::cli::snap_to_pixels({1.5f, 2.25f, 3.0f, 1.5f}, 10, 10);
    EXPECT_EQ(inside.x, 1.0f);
    EXPECT_EQ(inside.y, 2.0f);
    EXPECT_EQ(inside.w, 4.0f);
    EXPECT_EQ(inside.h, 2.0f);

    const engine::render::Rect clipped = engine::cli::snap_to_pixels({-3.0f, 8.0f, 6.0f, 6.0f}, 10, 10);
    EXPECT_EQ(clipped.x, 0.0f);
    EXPECT_EQ(clipped.y, 8.0f);
    EXPECT_EQ(clipped.w, 3.0f);
    EXPECT_EQ(clipped.h, 2.0f);

    const engine::render::Rect outside = engine::cli::snap_to_pixels({20.0f, 0.0f, 5.0f, 5.0f}, 10, 10);
    EXPECT_EQ(outside.w, 0.0f);
}

TEST(Cli, ScreenshotWritesTheWholeWindow) {
    ScratchDir dir;
    const std::string path = dir.file("whole.png");
    const std::string json = engine::cli::screenshot_json(nullptr, screenshot_request(path), coordinate_image(6, 4));
    EXPECT_EQ(json, std::format(R"({{"ok":true,"result":{{"path":"{}","window":0,"width":6,"height":4,)"
                                R"("rect":{{"x":0,"y":0,"w":6,"h":4}}}}}})",
                                json_path(path)));
    const std::optional<engine::render::TextureDesc> png = read_png(path);
    ASSERT_TRUE(png.has_value());
    EXPECT_EQ(png->width, 6);
    EXPECT_EQ(png->height, 4);
    EXPECT_EQ(png->rgba, coordinate_image(6, 4).rgba);
}

TEST(Cli, ScreenshotCropsToTheElementBorderBox) {
    GameCanvas game = spawn_game();
    layout_instance(game.world, game.entity);
    ScratchDir dir;
    const std::string path = dir.file("label.png");

    const std::string json =
            engine::cli::screenshot_json(&game.world, screenshot_request(path, "#lab"), coordinate_image(200, 100));
    EXPECT_NE(json.find(R"("width":80,"height":20,"rect":{"x":4,"y":4,"w":80,"h":20})"), std::string::npos) << json;
    const std::optional<engine::render::TextureDesc> png = read_png(path);
    ASSERT_TRUE(png.has_value());
    ASSERT_EQ(png->width, 80);
    ASSERT_EQ(png->height, 20);
    EXPECT_EQ(png->rgba[0], 4);
    EXPECT_EQ(png->rgba[1], 4);
    const std::size_t last = png->rgba.size() - 4;
    EXPECT_EQ(png->rgba[last], 83);
    EXPECT_EQ(png->rgba[last + 1], 23);
}

TEST(Cli, ScreenshotMapsAScaledCanvasToWindowPixels) {
    engine::ecs::World world;
    const auto xml = engine::ui::parse_xml(R"(<Canvas><Button id="half"/></Canvas>)");
    ASSERT_TRUE(xml.has_value());
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css("Button { width: 50px; height: 20px; margin: 10px; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::ScaleWithScreenSize;
    canvas.reference_size = {100.0f, 50.0f};
    canvas.rect = {0.0f, 0.0f, 200.0f, 100.0f};
    const engine::ecs::Entity entity = engine::ui::spawn_canvas(world, canvas, *xml, std::move(*sheet));
    engine::ui::UiInstance &instance = world.get<engine::ui::UiInstance>(entity);
    const engine::ui::UiCanvasSpace space =
            engine::ui::canvas_layout_space(canvas.rect, canvas.fit, canvas.reference_size);
    engine::ui::apply_layout_style(instance.document.root, &*instance.stylesheet, 100.0f, 50.0f);
    engine::ui::layout(instance.document, space.layout_rect);

    const std::expected<engine::render::Rect, std::string> rect =
            engine::cli::element_window_rect(world, screenshot_request({}, "#half"));
    ASSERT_TRUE(rect.has_value()) << rect.error();
    EXPECT_FLOAT_EQ(rect->x, space.offset.x + 10.0f * space.scale);
    EXPECT_FLOAT_EQ(rect->w, 50.0f * space.scale);
    EXPECT_FLOAT_EQ(space.scale, 2.0f);
}

TEST(Cli, ScreenshotRefusals) {
    EXPECT_EQ(engine::cli::screenshot_request_error(screenshot_request("")),
              std::optional<std::string>(R"({"ok":false,"error":"screenshot needs an absolute path"})"));
    EXPECT_TRUE(engine::cli::screenshot_request_error(screenshot_request("shot.png")).has_value());

    ScratchDir dir;
    const engine::render::TextureDesc image = coordinate_image(200, 100);
    EXPECT_FALSE(engine::cli::screenshot_request_error(screenshot_request(dir.file("a.png"))).has_value());
    EXPECT_EQ(engine::cli::screenshot_json(nullptr, screenshot_request(dir.file("a.png"), "#go"), image),
              R"({"ok":false,"error":"no world on window 0"})");

    GameCanvas game = spawn_game();
    layout_instance(game.world, game.entity);
    EXPECT_EQ(engine::cli::screenshot_json(&game.world, screenshot_request(dir.file("a.png"), "#nope"), image),
              R"({"ok":false,"error":"no element"})");
    EXPECT_EQ(engine::cli::screenshot_json(&game.world, screenshot_request(dir.file("a.png"), "#go"),
                                           coordinate_image(0, 0)),
              R"({"ok":false,"error":"element is outside the window"})");
    const std::string missing = dir.file("no/such/dir/a.png");
    const std::string unwritable = engine::cli::screenshot_json(nullptr, screenshot_request(missing), image);
    EXPECT_NE(unwritable.find("\"error\":\"could not write "), std::string::npos) << unwritable;
    EXPECT_FALSE(std::filesystem::exists(dir.path / "a.png"));
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

TEST_F(CliLoopback, ScreenshotReadsTheNextFrame) {
    const cli_client::Descriptor descriptor = cli_client::read_descriptor();
    ASSERT_GT(descriptor.port, 0);
    ScratchDir dir;
    const std::string path = dir.file("frame.png");

    GameCanvas game = spawn_game();
    layout_instance(game.world, game.entity);
    cli_client::Reply reply;
    std::thread client([&] {
        reply = cli_client::post_authorized(
                descriptor,
                std::format(R"({{"command":"screenshot","selector":"#go","path":"{}"}})", json_path(path)));
    });
    engine::cli::wait_for_request();
    EXPECT_TRUE(engine::cli::capture_requests().empty());
    // The first drain arms it; draw_all then reads the window it names.
    engine::cli::drain(frame_of(game.world));
    const std::vector<engine::WindowId> windows = engine::cli::capture_requests();
    ASSERT_EQ(windows.size(), 1u);
    EXPECT_EQ(windows.front(), engine::kPrimaryWindow);
    EXPECT_FALSE(std::filesystem::exists(dir.path / "frame.png"));

    const std::vector<engine::FrameCapture> captures{
            engine::FrameCapture{.window = engine::kPrimaryWindow, .image = coordinate_image(200, 100)}};
    engine::cli::CliFrame frame = frame_of(game.world);
    frame.captures = captures;
    engine::cli::drain(frame);
    client.join();
    EXPECT_EQ(reply.status, 200);
    EXPECT_NE(reply.body.find(R"("rect":{"x":0,"y":0,"w":100,"h":40})"), std::string::npos) << reply.body;
    const std::optional<engine::render::TextureDesc> png = read_png(path);
    ASSERT_TRUE(png.has_value());
    EXPECT_EQ(png->width, 100);
    EXPECT_EQ(png->height, 40);
    EXPECT_TRUE(engine::cli::capture_requests().empty());
}

TEST_F(CliLoopback, ScreenshotOfAWindowThatDidNotDraw) {
    const cli_client::Descriptor descriptor = cli_client::read_descriptor();
    ASSERT_GT(descriptor.port, 0);
    ScratchDir dir;
    const std::string path = json_path(dir.file("none.png"));
    const engine::cli::CliFrame frame{.world_for = [](engine::WindowId) -> engine::ecs::World * { return nullptr; }};

    cli_client::Reply reply;
    std::thread client([&] {
        reply = cli_client::post_authorized(
                descriptor, std::format(R"({{"command":"screenshot","window":3,"path":"{}"}})", path));
    });
    engine::cli::wait_for_request();
    engine::cli::drain(frame);
    ASSERT_EQ(engine::cli::capture_requests().size(), 1u);
    engine::cli::drain(frame);
    client.join();
    EXPECT_EQ(reply.body,
              R"({"ok":false,"error":"window 3 drew nothing: it is closed, hidden, or minimized"})");

    cli_client::Reply relative;
    std::thread second([&] {
        relative = cli_client::post_authorized(descriptor, R"({"command":"screenshot","path":"shot.png"})");
    });
    engine::cli::wait_for_request();
    engine::cli::drain(frame);
    second.join();
    EXPECT_EQ(relative.body, R"({"ok":false,"error":"screenshot needs an absolute path"})");
    EXPECT_TRUE(engine::cli::capture_requests().empty());
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
