#include <gtest/gtest.h>

#include "ui/element_path.h"
#include "ui/painter.h"

#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(NANOVG_H) || defined(NANOVG_GL_H) || defined(NANOVG_GL3)
#error "ui inspector tests must not include nvg headers"
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

    class CountingPainter final : public engine::ui::IUiPainter {
    public:
        int fills = 0;
        int strokes = 0;
        int texts = 0;
        glm::vec4 last_stroke{};
        engine::render::Rect last_fill{};
        std::string last_text;

        void save() override {}
        void restore() override {}
        void scissor(const engine::render::Rect &) override {}
        void apply_transform(glm::vec2, float, float) override {}
        void apply_view(glm::vec2, glm::vec2, float) override {}
        void set_opacity(float) override {}
        void fill_rounded_rect(const engine::render::Rect &rect, float, glm::vec4) override {
            ++fills;
            last_fill = rect;
        }
        void fill_rounded_rect_gradient(const engine::render::Rect &, float, const engine::ui::Gradient &) override {}
        void stroke_rounded_rect(const engine::render::Rect &, float, float, glm::vec4 color) override {
            ++strokes;
            last_stroke = color;
        }
        void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
        void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
        void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override {}
        void set_font(engine::AssetId, float) override {}
        void fill_text(std::string_view text, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {
            ++texts;
            last_text = std::string(text);
        }
        void image(engine::AssetId, const engine::render::Rect &) override {}
        void image_repeat(engine::AssetId, const engine::render::Rect &) override {}
        void image_nine_slice(engine::AssetId, const engine::render::Rect &, const engine::ui::BoxInsets &) override {}
        glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
            return {static_cast<float>(text.size()) * size * 0.5f, size};
        }
    };

    void expect_rect(const engine::render::Rect &rect, float x, float y, float w, float h) {
        EXPECT_NEAR(rect.x, x, 0.01f);
        EXPECT_NEAR(rect.y, y, 0.01f);
        EXPECT_NEAR(rect.w, w, 0.01f);
        EXPECT_NEAR(rect.h, h, 0.01f);
    }

    engine::ui::Element &add_child(engine::ui::Element &parent, engine::ui::ElementKind kind,
                                   engine::render::Rect rect) {
        engine::ui::Element &child = parent.children.emplace_back();
        child.kind = kind;
        child.layout_rect = rect;
        return child;
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
        for (engine::ui::Element &child: element.generated_items) {
            if (engine::ui::Element *found = find_id(child, id)) {
                return found;
            }
        }
        return nullptr;
    }

    engine::ui::Element *find_text(engine::ui::Element &element, std::string_view needle) {
        if (element.text.find(needle) != std::string::npos) {
            return &element;
        }
        for (engine::ui::Element &child: element.children) {
            if (engine::ui::Element *found = find_text(child, needle)) {
                return found;
            }
        }
        for (engine::ui::Element &child: element.generated_items) {
            if (engine::ui::Element *found = find_text(child, needle)) {
                return found;
            }
        }
        return nullptr;
    }

    engine::ui::Length px(float value) { return engine::ui::Length{value, engine::ui::LengthUnit::Px}; }

    struct GameCanvas {
        engine::ecs::World world;
        std::shared_ptr<ClickViewModel> vm = std::make_shared<ClickViewModel>();
        engine::ecs::Entity entity{};
    };

    GameCanvas spawn_game(engine::render::Rect rect) {
        GameCanvas game;
        engine::ui::presentation_of(game.world).sizes.sizes[engine::kPrimaryWindow] = {800, 600};
        const auto parsed = engine::ui::parse_xml(
                R"(<Canvas><Button id="go" command="{binding click}"><Label id="lab">Go</Label></Button></Canvas>)");
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
        canvas.rect = rect;
        canvas.data_context = game.vm;
        game.entity = engine::ui::spawn_canvas(game.world, canvas, *parsed, std::move(*sheet));
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

    class CellViewModel final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::string> name;

        CellViewModel() { property(engine::ui::intern("name"), name); }
    };

    class ListViewModel final : public engine::ui::ViewModel {
    public:
        engine::ui::BindableList<std::shared_ptr<CellViewModel>> cells;

        ListViewModel() { property(engine::ui::intern("cells"), cells); }
    };

    std::shared_ptr<CellViewModel> make_cell(std::string name) {
        auto cell = std::make_shared<CellViewModel>();
        cell->name.set(std::move(name));
        return cell;
    }

    struct ListCanvas {
        engine::ecs::World world;
        std::shared_ptr<ListViewModel> vm = std::make_shared<ListViewModel>();
        engine::ecs::Entity entity{};
    };

    // A canvas with an ItemsControl whose rows are a Stack with one Label each.
    std::unique_ptr<ListCanvas> spawn_list() {
        auto list = std::make_unique<ListCanvas>();
        engine::ui::presentation_of(list->world).sizes.sizes[engine::kPrimaryWindow] = {800, 600};
        const auto parsed = engine::ui::parse_xml(R"(<Canvas><ItemsControl id="list" items_source="{binding cells}">)"
                                                  R"(<ItemTemplate><Stack class="row"><Label text="{binding name}"/>)"
                                                  R"(</Stack></ItemTemplate></ItemsControl></Canvas>)");
        EXPECT_TRUE(parsed.has_value());
        list->vm->cells.set({make_cell("a"), make_cell("b")});
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.rect = {0.0f, 0.0f, 800.0f, 600.0f};
        canvas.data_context = list->vm;
        list->entity = engine::ui::spawn_canvas(list->world, canvas, *parsed);
        layout_instance(list->world, list->entity);
        return list;
    }

    const engine::ui::InspectorTreeRow *find_row(const std::vector<engine::ui::InspectorTreeRow> &rows,
                                                 std::string_view label) {
        for (const engine::ui::InspectorTreeRow &row: rows) {
            if (row.label.find(label) != std::string::npos) {
                return &row;
            }
        }
        return nullptr;
    }

    const engine::ui::InspectorTreeRow *selected_row(const std::vector<engine::ui::InspectorTreeRow> &rows) {
        for (const engine::ui::InspectorTreeRow &row: rows) {
            if (row.selected) {
                return &row;
            }
        }
        return nullptr;
    }

    engine::ui::Element *label_with_text(engine::ui::Element &element, std::string_view text) {
        if (element.kind == engine::ui::ElementKind::Label && element.text == text) {
            return &element;
        }
        for (engine::ui::Element &child: element.children) {
            if (engine::ui::Element *found = label_with_text(child, text)) {
                return found;
            }
        }
        for (engine::ui::Element &child: element.generated_items) {
            if (engine::ui::Element *found = label_with_text(child, text)) {
                return found;
            }
        }
        return nullptr;
    }

} // namespace

TEST(UiInspector, VisualHitDeepestZIndexAndHidden) {
    engine::ui::Element root;
    root.layout_rect = {0.0f, 0.0f, 100.0f, 100.0f};
    engine::ui::Element &stack = add_child(root, engine::ui::ElementKind::Stack, {0.0f, 0.0f, 80.0f, 80.0f});
    engine::ui::Element &label = add_child(stack, engine::ui::ElementKind::Label, {10.0f, 10.0f, 40.0f, 20.0f});
    label.id = "lab";

    const engine::ui::VisualHit deep = engine::ui::hit_test_visual(root, 12.0f, 12.0f);
    ASSERT_NE(deep.element, nullptr);
    EXPECT_EQ(deep.element->id, "lab");
    EXPECT_EQ(engine::ui::hit_test(root, 12.0f, 12.0f), nullptr);

    engine::ui::Element &low = add_child(root, engine::ui::ElementKind::Label, {0.0f, 0.0f, 50.0f, 50.0f});
    low.id = "low";
    engine::ui::Element &high = add_child(root, engine::ui::ElementKind::Label, {0.0f, 0.0f, 50.0f, 50.0f});
    high.id = "high";
    high.z_index = 3;
    EXPECT_EQ(engine::ui::hit_test_visual(root, 4.0f, 4.0f).element->id, "high");

    high.display_none = true;
    EXPECT_EQ(engine::ui::hit_test_visual(root, 4.0f, 4.0f).element->id, "low");

    high.display_none = false;
    high.visible = false;
    EXPECT_EQ(engine::ui::hit_test_visual(root, 4.0f, 4.0f).element->id, "low");
}

TEST(UiInspector, VisualHitScrollViewportAndGenerated) {
    engine::ui::Element scrolled;
    scrolled.layout_rect = {0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::Element &scroller = add_child(scrolled, engine::ui::ElementKind::Stack, {0.0f, 0.0f, 200.0f, 200.0f});
    scroller.scroll_y = 50.0f;
    scroller.overflow_y = engine::ui::Overflow::Scroll;
    engine::ui::Element &row = add_child(scroller, engine::ui::ElementKind::Label, {0.0f, 80.0f, 40.0f, 20.0f});
    row.id = "row";
    const engine::ui::VisualHit scrolled_hit = engine::ui::hit_test_visual(scrolled, 10.0f, 30.0f);
    ASSERT_NE(scrolled_hit.element, nullptr);
    EXPECT_EQ(scrolled_hit.element->id, "row");
    expect_rect(scrolled_hit.boxes.border, 0.0f, 30.0f, 40.0f, 20.0f);

    engine::ui::Element viewed;
    viewed.layout_rect = {0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::Element &viewport = add_child(viewed, engine::ui::ElementKind::Viewport, {0.0f, 0.0f, 200.0f, 200.0f});
    viewport.zoom = 2.0f;
    engine::ui::Element &tile = add_child(viewport, engine::ui::ElementKind::Label, {10.0f, 10.0f, 20.0f, 20.0f});
    tile.id = "tile";
    const engine::ui::VisualHit viewed_hit = engine::ui::hit_test_visual(viewed, 20.0f, 20.0f);
    ASSERT_NE(viewed_hit.element, nullptr);
    EXPECT_EQ(viewed_hit.element->id, "tile");
    expect_rect(viewed_hit.boxes.border, 20.0f, 20.0f, 40.0f, 40.0f);

    int owner = 7;
    engine::ui::Element generated;
    generated.layout_rect = {0.0f, 0.0f, 100.0f, 100.0f};
    engine::ui::Element &host =
            add_child(generated, engine::ui::ElementKind::ItemsControl, {0.0f, 0.0f, 100.0f, 100.0f});
    engine::ui::Element &templ = add_child(host, engine::ui::ElementKind::ItemTemplate, {0.0f, 0.0f, 100.0f, 100.0f});
    templ.id = "tmpl";
    engine::ui::Element &item = host.generated_items.emplace_back();
    item.kind = engine::ui::ElementKind::Label;
    item.id = "gen";
    item.generated_owner = &owner;
    item.layout_rect = {0.0f, 0.0f, 40.0f, 20.0f};
    const engine::ui::VisualHit generated_hit = engine::ui::hit_test_visual(generated, 5.0f, 5.0f);
    ASSERT_NE(generated_hit.element, nullptr);
    EXPECT_EQ(generated_hit.element->id, "gen");
}

TEST(UiInspector, ElementPathRoundTripAndOwnerRetarget) {
    int owner_a = 1;
    int owner_b = 2;
    engine::ui::Element root;
    engine::ui::Element &host = root.children.emplace_back();
    host.kind = engine::ui::ElementKind::ItemsControl;
    host.generated_items.reserve(2);
    engine::ui::Element &first = host.generated_items.emplace_back();
    first.generated_owner = &owner_a;
    first.id = "row-a";
    engine::ui::Element &cell = first.children.emplace_back();
    cell.kind = engine::ui::ElementKind::Label;
    cell.id = "cell";
    engine::ui::Element &second = host.generated_items.emplace_back();
    second.generated_owner = &owner_b;
    second.id = "row-b";

    const std::vector<std::size_t> path = engine::ui::find_element_path(root, &cell);
    ASSERT_EQ(path.size(), 3u);
    EXPECT_EQ(path[0], 0u);
    EXPECT_EQ(path[1], 0u | engine::ui::kGeneratedPathBit);
    EXPECT_EQ(path[2], 0u);
    EXPECT_EQ(engine::ui::resolve_element_path(root, path), &cell);
    EXPECT_EQ(engine::ui::path_generated_owner(root, path), static_cast<const void *>(&owner_a));

    std::swap(host.generated_items[0], host.generated_items[1]);
    EXPECT_EQ(engine::ui::resolve_element_path(root, path), nullptr);
    engine::ui::Element *retargeted = engine::ui::resolve_inspector_element(root, path, &owner_a);
    ASSERT_NE(retargeted, nullptr);
    EXPECT_EQ(retargeted->id, "cell");
}

TEST(UiInspector, BoxesInsetPaddingAndExpandMargin) {
    engine::ui::Element root;
    root.layout_rect = {0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::Element &child = add_child(root, engine::ui::ElementKind::Label, {5.0f, 5.0f, 100.0f, 80.0f});
    child.padding = {px(10.0f), px(10.0f), px(10.0f), px(10.0f)};
    child.margin = {px(5.0f), px(5.0f), px(5.0f), px(5.0f)};

    const engine::ui::VisualHit hit = engine::ui::hit_test_visual(root, 50.0f, 50.0f);
    ASSERT_EQ(hit.element, &child);
    expect_rect(hit.boxes.border, 5.0f, 5.0f, 100.0f, 80.0f);
    expect_rect(hit.boxes.margin, 0.0f, 0.0f, 110.0f, 90.0f);
    expect_rect(hit.boxes.content, 15.0f, 15.0f, 80.0f, 60.0f);

    const engine::ui::LayoutBoxes boxes = engine::ui::layout_boxes(root, child);
    expect_rect(boxes.margin, 0.0f, 0.0f, 110.0f, 90.0f);
    expect_rect(boxes.content, 15.0f, 15.0f, 80.0f, 60.0f);
}

TEST(UiInspector, MatchedRulesKeepTheWinnerAndDropMediaAndHover) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css("Label { color: #ffffff; }\n"
                                             "#hud Label.title { color: #ff0000; }\n"
                                             "@media (min-width: 2000px) { Label.title { color: #00ff00; } }\n"
                                             "Label:hover { color: #0000ff; }\n",
                                             warnings);
    ASSERT_TRUE(sheet.has_value());

    engine::ui::Element root;
    root.kind = engine::ui::ElementKind::Stack;
    root.id = "hud";
    engine::ui::Element &label = root.children.emplace_back();
    label.kind = engine::ui::ElementKind::Label;
    label.classes.push_back("title");

    const std::vector<engine::ui::MatchedRule> rules =
            engine::ui::match_style_rules(label, &*sheet, {&root}, 800.0f, 600.0f);
    ASSERT_EQ(rules.size(), 2u);
    EXPECT_EQ(rules.back().specificity, 7);
    EXPECT_NE(rules.back().selector.find("#hud"), std::string::npos);
    EXPECT_NE(rules.back().selector.find("title"), std::string::npos);
    for (const engine::ui::MatchedRule &rule: rules) {
        EXPECT_EQ(rule.selector.find("hover"), std::string::npos);
        EXPECT_EQ(rule.selector.find("2000"), std::string::npos);
    }
}

TEST(UiInspector, AttachStartsWithPickOffAndDetachClears) {
    GameCanvas game = spawn_game({0.0f, 0.0f, 800.0f, 600.0f});
    EXPECT_FALSE(engine::ui::inspector_attached(game.world));
    EXPECT_TRUE(engine::ui::inspector_tree(game.world).empty());

    engine::ui::set_inspector_attached(game.world, true);
    EXPECT_TRUE(engine::ui::inspector_attached(game.world));
    EXPECT_FALSE(game.world.ctx<engine::ui::UiInspector>().pick_pointer);
    game.world.ctx<engine::ui::UiInspector>().pick_pointer = true;
    engine::ui::inspector_select(game.world, engine::kPrimaryWindow, engine::ui::InspectorPick{.canvas = game.entity});
    EXPECT_TRUE(engine::ui::inspector_selection(game.world).active);

    engine::ui::set_inspector_attached(game.world, true);
    EXPECT_TRUE(engine::ui::inspector_selection(game.world).active) << "attaching twice keeps the state";

    engine::ui::set_inspector_attached(game.world, false);
    EXPECT_FALSE(engine::ui::inspector_attached(game.world));
    EXPECT_FALSE(engine::ui::inspector_selection(game.world).active);
    EXPECT_FALSE(game.world.ctx<engine::ui::UiInspector>().pick_pointer);
    EXPECT_TRUE(engine::ui::inspector_tree(game.world).empty());
}

TEST(UiInspector, ClickOnLabelSelectsItAndSkipsTheCommand) {
    GameCanvas game = spawn_game({0.0f, 0.0f, 800.0f, 600.0f});
    layout_instance(game.world, game.entity);
    engine::ui::UiInstance &instance = game.world.get<engine::ui::UiInstance>(game.entity);
    engine::ui::Element *label = find_id(instance.document.root, "lab");
    ASSERT_NE(label, nullptr);
    ASSERT_GT(label->layout_rect.w, 1.0f);
    const float x = label->layout_rect.x + label->layout_rect.w * 0.5f;
    const float y = label->layout_rect.y + label->layout_rect.h * 0.5f;
    EXPECT_EQ(engine::ui::hit_test(instance.document.root, x, y)->id, "go");
    EXPECT_EQ(engine::ui::hit_test_visual(instance.document.root, x, y).element->id, "lab");

    engine::ui::set_inspector_attached(game.world, true);
    game.world.ctx<engine::ui::UiInspector>().pick_pointer = true;
    engine::ui::handle_pointer(game.world, x, y);
    EXPECT_EQ(game.vm->clicks, 0);
    EXPECT_TRUE(engine::ui::presentation_of(game.world).mouse.consumed_for());

    const engine::ui::InspectorPick pick = engine::ui::inspector_selection(game.world);
    EXPECT_TRUE(pick.active);
    EXPECT_EQ(pick.canvas, game.entity);
    engine::ui::UiInstance &live = game.world.get<engine::ui::UiInstance>(game.entity);
    engine::ui::Element *selected = engine::ui::resolve_element_path(live.document.root, pick.path);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->id, "lab");

    CountingPainter painter;
    const engine::ui::Stylesheet *sheet = live.stylesheet ? &*live.stylesheet : nullptr;
    engine::ui::paint_document(live.document, sheet, painter,
                               engine::ui::UiPaintInput{
                                       .canvas_rect = {0.0f, 0.0f, 800.0f, 600.0f},
                                       .window_width = 800.0f,
                                       .window_height = 600.0f,
                               });
    const std::string detail = engine::ui::inspector_detail(game.world, pick);
    EXPECT_NE(detail.find("Label #lab"), std::string::npos) << detail;
    EXPECT_NE(detail.find("1.00,0.00,0.00,1.00"), std::string::npos) << detail;
    EXPECT_NE(detail.find("rules: 2"), std::string::npos) << detail;

    const std::vector<std::string> rules = engine::ui::inspector_rules(game.world, pick);
    ASSERT_EQ(rules.size(), 2u);
    EXPECT_NE(rules.back().find("#go"), std::string::npos);
    EXPECT_NE(rules.back().find("winner"), std::string::npos);
    EXPECT_NE(rules.back().find("color: #ff0000"), std::string::npos);
    EXPECT_EQ(rules.front().find("winner"), std::string::npos);
    for (const std::string &rule: rules) {
        EXPECT_EQ(rule.find(":hover"), std::string::npos);
        EXPECT_EQ(rule.find("#00ff00"), std::string::npos) << "@media (min-width: 4000px) is off at 800px";
    }

    engine::ui::handle_pointer(game.world, 700.0f, 10.0f);
    EXPECT_EQ(game.vm->clicks, 0);
    const engine::ui::InspectorPick edge = engine::ui::inspector_selection(game.world);
    EXPECT_TRUE(edge.active);
    EXPECT_TRUE(edge.path.empty());
    EXPECT_EQ(edge.canvas, game.entity);
    engine::ui::set_inspector_attached(game.world, false);
}

TEST(UiInspector, DetailOfNothingAndOfAGoneElement) {
    GameCanvas game = spawn_game({0.0f, 0.0f, 800.0f, 600.0f});
    engine::ui::set_inspector_attached(game.world, true);
    EXPECT_EQ(engine::ui::inspector_detail(game.world, {}), "Nothing selected");
    EXPECT_TRUE(engine::ui::inspector_rules(game.world, {}).empty());

    engine::ui::InspectorPick gone{.canvas = game.entity, .path = {7, 3}, .active = true};
    EXPECT_EQ(engine::ui::inspector_detail(game.world, gone), "Selected element is not in the live tree.");
    EXPECT_TRUE(engine::ui::inspector_rules(game.world, gone).empty());

    engine::ui::inspector_select(game.world, engine::kPrimaryWindow, engine::ui::InspectorPick{.canvas = game.entity});
    game.world.destroy(game.entity);
    engine::ui::inspector_retarget(game.world);
    EXPECT_FALSE(engine::ui::inspector_selection(game.world).active) << "the canvas is gone";
}

TEST(UiInspector, NotAttachedOrPickOffLetsTheButtonRun) {
    GameCanvas game = spawn_game({0.0f, 0.0f, 800.0f, 600.0f});
    layout_instance(game.world, game.entity);
    engine::ui::UiInstance &instance = game.world.get<engine::ui::UiInstance>(game.entity);
    engine::ui::Element *button = find_id(instance.document.root, "go");
    ASSERT_NE(button, nullptr);
    const float x = button->layout_rect.x + button->layout_rect.w * 0.5f;
    const float y = button->layout_rect.y + button->layout_rect.h * 0.5f;

    game.world.ctx<engine::ui::UiInspector>().pick_pointer = true;
    engine::ui::handle_pointer(game.world, x, y);
    EXPECT_EQ(game.vm->clicks, 1) << "pick does nothing on a world that is not attached";

    engine::ui::set_inspector_attached(game.world, true);
    engine::ui::handle_pointer(game.world, x, y);
    EXPECT_EQ(game.vm->clicks, 2) << "attaching starts with pick off";
    EXPECT_FALSE(engine::ui::inspector_selection(game.world).active);
    engine::ui::set_inspector_attached(game.world, false);
}

TEST(UiInspector, MissDoesNotConsume) {
    GameCanvas game = spawn_game({0.0f, 0.0f, 100.0f, 100.0f});
    engine::ui::set_inspector_attached(game.world, true);
    game.world.ctx<engine::ui::UiInspector>().pick_pointer = true;
    engine::ui::handle_pointer(game.world, 200.0f, 200.0f);
    EXPECT_EQ(game.vm->clicks, 0);
    EXPECT_FALSE(engine::ui::presentation_of(game.world).mouse.consumed_for());
    EXPECT_FALSE(engine::ui::inspector_selection(game.world).active);
    engine::ui::set_inspector_attached(game.world, false);
}

TEST(UiInspector, TreeRowsSelectAndToggle) {
    GameCanvas game = spawn_game({0.0f, 0.0f, 800.0f, 600.0f});
    engine::ui::set_inspector_attached(game.world, true);

    std::vector<engine::ui::InspectorTreeRow> rows = engine::ui::inspector_tree(game.world);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].label, "Canvas");
    EXPECT_EQ(rows[0].tree.depth, 0);
    EXPECT_TRUE(rows[0].tree.has_children);
    EXPECT_TRUE(rows[0].tree.expanded);
    EXPECT_EQ(rows[1].label, "Button #go");
    EXPECT_EQ(rows[1].tree.depth, 1);
    EXPECT_EQ(rows[2].label, "Label #lab");
    EXPECT_EQ(rows[2].tree.depth, 2);
    EXPECT_FALSE(rows[2].tree.has_children);
    EXPECT_EQ(selected_row(rows), nullptr);

    engine::ui::inspector_select(game.world, rows[2].window, rows[2].pick);
    const engine::ui::InspectorPick pick = engine::ui::inspector_selection(game.world);
    EXPECT_TRUE(pick.active);
    EXPECT_NE(engine::ui::inspector_detail(game.world, pick).find("Label #lab"), std::string::npos);
    rows = engine::ui::inspector_tree(game.world);
    ASSERT_NE(selected_row(rows), nullptr);
    EXPECT_EQ(selected_row(rows)->label, "Label #lab");

    engine::ui::inspector_toggle(game.world, rows[1].key);
    rows = engine::ui::inspector_tree(game.world);
    ASSERT_EQ(rows.size(), 2u) << "a collapsed row hides its children";
    EXPECT_FALSE(rows[1].tree.expanded);
    engine::ui::inspector_toggle(game.world, rows[1].key);
    EXPECT_EQ(engine::ui::inspector_tree(game.world).size(), 3u);

    const engine::ui::InspectorRowKey leaf = engine::ui::inspector_tree(game.world)[2].key;
    engine::ui::inspector_toggle(game.world, leaf);
    rows = engine::ui::inspector_tree(game.world);
    ASSERT_EQ(rows.size(), 3u) << "a leaf has nothing to collapse";
    EXPECT_FALSE(rows[2].tree.expanded);
    EXPECT_EQ(rows[2].tree.parent, 1u);
    engine::ui::set_inspector_attached(game.world, false);
}

TEST(UiInspector, SelectionsCountSelectsAndPickClicksButNotRetargets) {
    GameCanvas game = spawn_game({0.0f, 0.0f, 800.0f, 600.0f});
    layout_instance(game.world, game.entity);
    engine::ui::set_inspector_attached(game.world, true);
    const engine::ui::UiInspector &inspector = game.world.ctx<engine::ui::UiInspector>();
    EXPECT_EQ(inspector.selections, 0u);

    const std::vector<engine::ui::InspectorTreeRow> rows = engine::ui::inspector_tree(game.world);
    ASSERT_EQ(rows.size(), 3u);
    engine::ui::inspector_select(game.world, rows[2].window, rows[2].pick);
    EXPECT_EQ(inspector.selections, 1u);
    engine::ui::inspector_select(game.world, rows[2].window, rows[2].pick);
    EXPECT_EQ(inspector.selections, 2u) << "selecting the same element again is a new selection";

    engine::ui::inspector_retarget(game.world);
    (void) engine::ui::inspector_tree(game.world);
    EXPECT_EQ(inspector.selections, 2u);

    game.world.ctx<engine::ui::UiInspector>().pick_pointer = true;
    engine::ui::handle_pointer(game.world, 700.0f, 10.0f);
    EXPECT_EQ(inspector.selections, 3u) << "a pick click selects";

    engine::ui::set_inspector_attached(game.world, false);
    EXPECT_EQ(game.world.ctx<engine::ui::UiInspector>().selections, 0u);
}

TEST(UiInspector, GeneratedRowsKeepKeyAndSelectionWhenTheListMoves) {
    std::unique_ptr<ListCanvas> list = spawn_list();
    engine::ecs::World &world = list->world;
    engine::ui::set_inspector_attached(world, true);

    std::vector<engine::ui::InspectorTreeRow> rows = engine::ui::inspector_tree(world);
    // Canvas, ItemsControl, ItemTemplate (+ its Stack and Label), then two generated rows with a Label each.
    const engine::ui::InspectorTreeRow *row_b = nullptr;
    int stacks = 0;
    for (const engine::ui::InspectorTreeRow &row: rows) {
        if (row.label == "Stack .row" && row.key.owner != 0) {
            ++stacks;
            if (stacks == 2) {
                row_b = &row;
            }
        }
    }
    ASSERT_EQ(stacks, 2);
    ASSERT_NE(row_b, nullptr);
    EXPECT_TRUE(row_b->key.relative.empty()) << "a generated row is keyed by its owner";
    const engine::ui::InspectorRowKey key_b = row_b->key;

    engine::ui::UiInstance &instance = world.get<engine::ui::UiInstance>(list->entity);
    engine::ui::Element *label_b = label_with_text(instance.document.root, "b");
    ASSERT_NE(label_b, nullptr);
    engine::ui::InspectorPick pick;
    pick.canvas = list->entity;
    pick.path = engine::ui::find_element_path(instance.document.root, label_b);
    pick.generated_owner = engine::ui::path_generated_owner(instance.document.root, pick.path);
    engine::ui::inspector_select(world, engine::kPrimaryWindow, pick);
    engine::ui::inspector_toggle(world, key_b);

    // Reorder and rebuild: row "b" moves to index 0.
    std::vector<std::shared_ptr<CellViewModel>> cells = list->vm->cells.get();
    std::swap(cells[0], cells[1]);
    cells.push_back(make_cell("c"));
    list->vm->cells.set(std::move(cells));
    layout_instance(world, list->entity);

    rows = engine::ui::inspector_tree(world);
    const engine::ui::InspectorPick moved = engine::ui::inspector_selection(world);
    ASSERT_TRUE(moved.active);
    EXPECT_NE(moved.path, pick.path) << "retarget follows the owner to its new index";
    engine::ui::Element *resolved =
            engine::ui::resolve_element_path(world.get<engine::ui::UiInstance>(list->entity).document.root, moved.path);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->text, "b");

    const engine::ui::InspectorTreeRow *collapsed = nullptr;
    for (const engine::ui::InspectorTreeRow &row: rows) {
        if (row.key == key_b) {
            collapsed = &row;
        }
    }
    ASSERT_NE(collapsed, nullptr) << "the key survives the move";
    EXPECT_FALSE(collapsed->tree.expanded);
    EXPECT_EQ(selected_row(rows), nullptr) << "the selected label is inside the collapsed row";

    engine::ui::inspector_toggle(world, key_b);
    rows = engine::ui::inspector_tree(world);
    ASSERT_NE(selected_row(rows), nullptr);
    EXPECT_EQ(selected_row(rows)->label, "Label");
    EXPECT_EQ(selected_row(rows)->pick.path, moved.path);
    engine::ui::set_inspector_attached(world, false);
}

TEST(UiInspector, TreeSpansWindowsAndPrefixesTheRoot) {
    GameCanvas game = spawn_game({0.0f, 0.0f, 800.0f, 600.0f});
    const auto parsed = engine::ui::parse_xml(R"(<Canvas id="tool"/>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = {0.0f, 0.0f, 100.0f, 100.0f};
    canvas.window = engine::WindowId{3};
    const engine::ecs::Entity tool = engine::ui::spawn_canvas(game.world, canvas, *parsed);
    engine::ui::set_inspector_attached(game.world, true);

    const std::vector<engine::ui::InspectorTreeRow> rows = engine::ui::inspector_tree(game.world);
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].label, "[0] Canvas");
    EXPECT_EQ(rows[3].label, "[3] Canvas #tool");
    EXPECT_EQ(rows[3].window, engine::WindowId{3});
    EXPECT_EQ(rows[3].key.canvas, tool);

    engine::ui::inspector_select(game.world, rows[3].window, rows[3].pick);
    EXPECT_EQ(game.world.ctx<engine::ui::UiInspector>().detail_window, engine::WindowId{3});
    EXPECT_TRUE(engine::ui::inspector_selection(game.world, engine::WindowId{3}).active);
    EXPECT_FALSE(engine::ui::inspector_selection(game.world).active);
    engine::ui::set_inspector_attached(game.world, false);
}

TEST(UiInspector, HoverCanvasIsTheTopCanvasUnderThePointer) {
    GameCanvas game = spawn_game({0.0f, 0.0f, 100.0f, 100.0f});
    const auto parsed = engine::ui::parse_xml(R"(<Canvas id="top"/>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = {0.0f, 0.0f, 50.0f, 50.0f};
    canvas.order = 5;
    const engine::ecs::Entity top = engine::ui::spawn_canvas(game.world, canvas, *parsed);

    engine::ui::pointer_for(game.world, engine::kPrimaryWindow).position = {10.0f, 10.0f};
    EXPECT_FALSE(engine::ui::inspector_hover_canvas(game.world, engine::kPrimaryWindow).has_value());
    engine::ui::set_inspector_attached(game.world, true);
    EXPECT_EQ(engine::ui::inspector_hover_canvas(game.world, engine::kPrimaryWindow), top);
    engine::ui::pointer_for(game.world, engine::kPrimaryWindow).position = {80.0f, 80.0f};
    EXPECT_EQ(engine::ui::inspector_hover_canvas(game.world, engine::kPrimaryWindow), game.entity);
    engine::ui::pointer_for(game.world, engine::kPrimaryWindow).position = {180.0f, 80.0f};
    EXPECT_FALSE(engine::ui::inspector_hover_canvas(game.world, engine::kPrimaryWindow).has_value());
    engine::ui::set_inspector_attached(game.world, false);
}

TEST(UiInspector, OverlayDrawsHoverAndSelectionOnceWhenTheyMatch) {
    const auto parsed = engine::ui::parse_xml("<Canvas/>");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument document = *parsed;
    CountingPainter painter;
    engine::ui::UiPaintInput input;
    input.canvas_rect = {0.0f, 0.0f, 100.0f, 100.0f};
    input.pointer = {10.0f, 10.0f};
    input.window_width = 100.0f;
    input.window_height = 100.0f;
    input.inspector_hover = true;
    engine::ui::paint_document(document, nullptr, painter, input);
    EXPECT_EQ(painter.fills, 4);
    EXPECT_EQ(painter.strokes, 1);
    EXPECT_EQ(painter.texts, 1);
    EXPECT_EQ(painter.last_text, "Canvas  100 × 100");
    EXPECT_GT(painter.last_fill.y, 100.0f);
    EXPECT_GE(painter.last_fill.x, 0.0f);

    painter.fills = 0;
    painter.strokes = 0;
    painter.texts = 0;
    input.inspector_selection = true;
    engine::ui::paint_document(document, nullptr, painter, input);
    EXPECT_EQ(painter.fills, 4);
    EXPECT_EQ(painter.strokes, 1);
    EXPECT_EQ(painter.texts, 1);
    EXPECT_EQ(painter.last_text, "Canvas  100 × 100");
    EXPECT_NEAR(painter.last_stroke.y, 0.9f, 0.01f);
    EXPECT_NEAR(painter.last_stroke.z, 0.4f, 0.01f);
}

TEST(UiInspector, ElementTagJoinsKindIdAndClasses) {
    engine::ui::Element stack;
    stack.kind = engine::ui::ElementKind::Stack;
    stack.classes = {"right-panel", "white"};
    EXPECT_EQ(engine::ui::inspector_element_tag(stack), "Stack.right-panel.white");
    stack.id = "hud";
    EXPECT_EQ(engine::ui::inspector_element_tag(stack), "Stack#hud.right-panel.white");
}

TEST(UiInspector, BadgeFollowsHoverAndPrintsATenth) {
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack id="hud" class="right-panel white"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css("#hud { width: 128.5px; height: 40px; margin: 0; }\n", warnings);
    ASSERT_TRUE(sheet.has_value());
    engine::ui::UiDocument document = *parsed;
    CountingPainter painter;
    engine::ui::UiPaintInput input;
    input.canvas_rect = {0.0f, 0.0f, 200.0f, 200.0f};
    input.pointer = {10.0f, 10.0f};
    input.window_width = 200.0f;
    input.window_height = 200.0f;
    input.inspector_hover = true;
    input.inspector_selection = true;
    engine::ui::paint_document(document, &*sheet, painter, input);
    EXPECT_EQ(painter.texts, 1);
    EXPECT_EQ(painter.last_text, "Stack#hud.right-panel.white  128.5 × 40");
    EXPECT_EQ(painter.fills, 7);
    engine::ui::Element *hud = find_id(document.root, "hud");
    ASSERT_NE(hud, nullptr);
    EXPECT_GT(painter.last_fill.y, hud->layout_rect.y + hud->layout_rect.h);
}
