#include <gtest/gtest.h>

#include "ui/dock_runtime.h"
#include "ui/painter.h"

#include <engine/core/input_system.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/dock_space.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// engine/ui/dock_space.h: the dock systems place panel canvases, draw chrome, and turn pointer gestures on that
// chrome into layout operations. docs/tech/features/Docking.md#host.

namespace {

using engine::kPrimaryWindow;
using engine::MouseButton;
using engine::MouseEvent;
using engine::render::Rect;
using engine::ui::DockDrop;
using engine::ui::DockGeometry;
using engine::ui::DockLayout;
using engine::ui::DockNodeId;
using engine::ui::DockSpace;
using engine::ui::DockTarget;
using engine::ui::DockZone;
using engine::ui::Element;
using engine::ui::UiCanvas;

constexpr Rect kWindow{0.0f, 0.0f, 800.0f, 600.0f};
constexpr int kBase = 10;

glm::vec2 center(const Rect& r) {
    return {r.x + r.w * 0.5f, r.y + r.h * 0.5f};
}

Rect moved(Rect r, glm::vec2 delta) {
    r.x += delta.x;
    r.y += delta.y;
    return r;
}

DockNodeId stack_of(const DockLayout& layout, std::string_view key) {
    return layout.find(key)->stack;
}

// `a` and `c` tabbed on the left (`c` active), `b` on the right.
DockLayout side_by_side() {
    DockLayout layout;
    layout.add("a", {});
    layout.add("b", {stack_of(layout, "a"), DockZone::Right});
    layout.add("c", {stack_of(layout, "a"), DockZone::Center});
    return layout;
}

// `a` docked; `b` and `c` tabbed in one float at (100, 100) 300 x 200.
DockLayout docked_and_float() {
    DockLayout layout;
    layout.add("a", {});
    layout.add("b", {stack_of(layout, "a"), DockZone::Right});
    layout.float_panel("b", Rect{100.0f, 100.0f, 300.0f, 200.0f});
    layout.add("c", {stack_of(layout, "b"), DockZone::Center});
    return layout;
}

engine::ui::Stylesheet default_theme() {
    std::ifstream in(std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "css" / "dock.css");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(text, warnings);
    EXPECT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty()) << (warnings.empty() ? "" : warnings[0]);
    return sheet.value_or(engine::ui::Stylesheet{});
}

// Text is half the font size per byte wide, so a title's width is easy to state.
class HalfEmPainter final : public engine::ui::IUiPainter {
public:
    void save() override {}
    void restore() override {}
    void scissor(const Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
    void set_opacity(float) override {}
    void fill_rounded_rect(const Rect&, float, glm::vec4) override {}
    void fill_rounded_rect_gradient(const Rect&, float, const engine::ui::Gradient&) override {}
    void stroke_rounded_rect(const Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
    void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
    void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override {}
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {}
    void image(engine::AssetId, const Rect&) override {}
    void image_repeat(engine::AssetId, const Rect&) override {}
    void image_nine_slice(engine::AssetId, const Rect&, const engine::ui::BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view text, engine::AssetId font, float size) override {
        fonts.push_back(font);
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }

    std::vector<engine::AssetId> fonts;
};

// Generated rows only: an ItemTemplate is not shown.
void collect_class(Element& element, std::string_view name, std::vector<Element*>& out) {
    if (element.kind == engine::ui::ElementKind::ItemTemplate) {
        return;
    }
    if (std::ranges::find(element.classes, name) != element.classes.end()) {
        out.push_back(&element);
    }
    for (Element& child : element.children) {
        collect_class(child, name, out);
    }
    for (Element& child : element.generated_items) {
        collect_class(child, name, out);
    }
}

class DockSpaceTest : public ::testing::Test {
protected:
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    std::vector<engine::ecs::Entity> panel_canvases;

    void SetUp() override {
        engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
        engine::ui::presentation_of(world).sizes.sizes[kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    }

    engine::ecs::Entity spawn_panel(engine::ui::Node root = engine::ui::canvas()) {
        UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::FillWindow;
        canvas.order = 500;
        return engine::ui::spawn_canvas(world, canvas, *engine::ui::make_document(std::move(root)));
    }

    // A space over the whole window with panels a, b, c registered, and one frame run.
    engine::ecs::Entity make_space(DockLayout layout, Rect area = kWindow) {
        DockSpace space;
        space.area = area;
        space.order = kBase;
        space.layout = std::move(layout);
        for (const char* key : {"a", "b", "c"}) {
            const engine::ecs::Entity canvas = spawn_panel();
            panel_canvases.push_back(canvas);
            space.panels.push_back(engine::ui::DockPanel{key, std::string("Panel ") + key, canvas});
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

    void send(MouseEvent::Kind kind, glm::vec2 p, MouseButton button = MouseButton::Left) {
        engine::ecs::EventWriter<MouseEvent>{world}.send(MouseEvent{
                .window = kPrimaryWindow,
                .kind = kind,
                .position = p,
                .button = kind == MouseEvent::Kind::Move ? MouseButton::None : button,
        });
    }

    void key(engine::KeyCode code, bool down) {
        engine::ecs::EventWriter<engine::KeyEvent>{world}.send(
                engine::KeyEvent{.window = kPrimaryWindow, .key = code, .down = down});
    }

    void press(glm::vec2 p, MouseButton button = MouseButton::Left) {
        send(MouseEvent::Kind::Down, p, button);
        frame();
    }

    void drag_to(glm::vec2 p) {
        send(MouseEvent::Kind::Move, p);
        frame();
    }

    void release(glm::vec2 p) {
        send(MouseEvent::Kind::Up, p);
        frame();
    }

    bool consumed() { return engine::ui::presentation_of(world).mouse.consumed_for(kPrimaryWindow); }

    DockSpace& space(engine::ecs::Entity entity) { return world.get<DockSpace>(entity); }

    UiCanvas& canvas(engine::ecs::Entity entity) { return world.get<UiCanvas>(entity); }

    UiCanvas& panel(char key) { return canvas(panel_canvases[static_cast<std::size_t>(key - 'a')]); }

    engine::ui::DockSpaceRuntime& runtime(engine::ecs::Entity entity) {
        return world.ctx<engine::ui::DockRuntime>().spaces.at(entity);
    }

    DockGeometry geometry(engine::ecs::Entity entity) {
        return engine::ui::dock_space_geometry(space(entity), runtime(entity));
    }

    Rect tab(engine::ecs::Entity entity, std::string_view key) {
        const DockGeometry g = geometry(entity);
        const engine::ui::DockStackRect* stack = g.stack(stack_of(space(entity).layout, key));
        return std::ranges::find(stack->tabs, key, &engine::ui::DockTabRect::key)->rect;
    }
};

TEST_F(DockSpaceTest, PlacesDockedPanelsAndHidesInactiveTabs) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const DockGeometry g = geometry(entity);

    EXPECT_EQ(panel('c').rect, g.panel("c")->content);
    EXPECT_EQ(panel('b').rect, g.panel("b")->content);
    EXPECT_EQ(panel('a').rect, Rect{});
    for (const char key : {'a', 'b', 'c'}) {
        EXPECT_EQ(panel(key).fit, engine::ui::UiFit::Fixed);
        EXPECT_EQ(panel(key).window, kPrimaryWindow);
        EXPECT_EQ(panel(key).order, kBase + 1);
    }
    // The docked chrome covers the area under the panels.
    EXPECT_EQ(canvas(runtime(entity).docked.canvas).rect, kWindow);
    EXPECT_EQ(canvas(runtime(entity).docked.canvas).order, kBase);
    EXPECT_EQ(canvas(runtime(entity).preview.canvas).rect, Rect{});
    EXPECT_EQ(canvas(runtime(entity).docked.canvas).extra_stylesheets,
            std::vector<engine::AssetId>{engine::builtin::dock_css});
}

TEST_F(DockSpaceTest, ChromeListsFollowTheGeometry) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const DockGeometry g = geometry(entity);
    const engine::ui::DockChromeViewModel& vm = *runtime(entity).docked.vm;

    ASSERT_EQ(vm.stacks.get().size(), 2u);
    ASSERT_EQ(vm.tabs.get().size(), 3u);
    ASSERT_EQ(vm.splitters.get().size(), 1u);
    EXPECT_TRUE(vm.frames.get().empty());
    const engine::ui::DockChromeItem& c = *vm.tabs.get()[1];
    EXPECT_EQ(c.title.get(), "Panel c");
    EXPECT_TRUE(c.active.get());
    EXPECT_FALSE(vm.tabs.get()[0]->active.get());
    EXPECT_EQ(c.x.get(), tab(entity, "c").x);
    EXPECT_EQ(c.w.get(), tab(entity, "c").w);
    EXPECT_EQ(c.close.get(), "none");
    EXPECT_EQ(vm.splitters.get()[0]->x.get(), g.splitters[0].grab.x);
}

TEST_F(DockSpaceTest, FloatsStackAboveTheDockedTreeAndEachOther) {
    DockLayout layout = docked_and_float();
    layout.float_panel("c", Rect{300.0f, 250.0f, 300.0f, 200.0f});
    const engine::ecs::Entity entity = make_space(std::move(layout));
    const auto floats = space(entity).layout.floats();
    ASSERT_EQ(floats.size(), 2u);
    const DockGeometry g = geometry(entity);

    EXPECT_EQ(panel('a').order, kBase + 1);
    EXPECT_EQ(panel('b').order, kBase + 3);
    EXPECT_EQ(panel('c').order, kBase + 5);
    EXPECT_EQ(panel('b').rect, g.panel("b")->content);
    const engine::ui::DockSpaceRuntime& rt = runtime(entity);
    ASSERT_EQ(rt.floats.size(), 2u);
    EXPECT_EQ(canvas(rt.floats.at(floats[0].id).canvas).order, kBase + 2);
    EXPECT_EQ(canvas(rt.floats.at(floats[0].id).canvas).rect, floats[0].rect);
    EXPECT_EQ(canvas(rt.floats.at(floats[1].id).canvas).order, kBase + 4);
    EXPECT_EQ(canvas(rt.preview.canvas).order, kBase + 6);
    EXPECT_EQ(engine::ui::dock_space_order_count(space(entity).layout), 7);
    const engine::ui::DockChromeViewModel& top = *rt.floats.at(floats[1].id).vm;
    ASSERT_EQ(top.frames.get().size(), 1u);
    EXPECT_EQ(top.frames.get()[0]->x.get(), 0.0f);
    EXPECT_EQ(top.frames.get()[0]->w.get(), 300.0f);
    ASSERT_EQ(top.titles.get().size(), 1u);
    EXPECT_EQ(top.titles.get()[0]->title.get(), "Panel c");
}

TEST_F(DockSpaceTest, ClickOnATabActivatesItAndConsumesTheMouse) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const std::uint64_t revision = space(entity).revision;

    press(center(tab(entity, "a")));
    EXPECT_TRUE(consumed());
    EXPECT_TRUE(space(entity).layout.is_visible("a"));
    EXPECT_EQ(space(entity).revision, revision + 1);
    EXPECT_EQ(panel('a').rect, geometry(entity).panel("a")->content);
    EXPECT_EQ(panel('c').rect, Rect{});
    release(center(tab(entity, "a")));
    EXPECT_EQ(space(entity).revision, revision + 1);

    // The active tab again changes nothing.
    press(center(tab(entity, "a")));
    release(center(tab(entity, "a")));
    EXPECT_EQ(space(entity).revision, revision + 1);
}

TEST_F(DockSpaceTest, TabDragDocksAtAStackEdgeWithALivePreview) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    DockLayout expected = space(entity).layout;
    const DockNodeId right = stack_of(expected, "b");
    const std::uint64_t revision = space(entity).revision;

    press(center(tab(entity, "c")));
    drag_to({600.0f, 520.0f});
    const std::optional<DockDrop>& drop = runtime(entity).gesture.drop;
    ASSERT_TRUE(drop.has_value());
    EXPECT_EQ(drop->target, (DockTarget{right, DockZone::Bottom}));
    EXPECT_EQ(canvas(runtime(entity).preview.canvas).rect, drop->preview);
    EXPECT_EQ(canvas(runtime(entity).preview.canvas).order, kBase + 2);
    EXPECT_TRUE(consumed());
    // Nothing moved yet.
    EXPECT_EQ(space(entity).layout, expected);

    release({600.0f, 520.0f});
    ASSERT_TRUE(expected.move("c", {right, DockZone::Bottom}));
    EXPECT_EQ(space(entity).layout, expected);
    EXPECT_EQ(space(entity).revision, revision + 1);
    EXPECT_EQ(canvas(runtime(entity).preview.canvas).rect, Rect{});
    EXPECT_EQ(panel('c').rect, geometry(entity).panel("c")->content);
}

TEST_F(DockSpaceTest, TabDragOntoAStripJoinsThatStack) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    DockLayout expected = space(entity).layout;

    press(center(tab(entity, "b")));
    drag_to({300.0f, 12.0f});
    release({300.0f, 12.0f});
    ASSERT_TRUE(expected.move("b", {stack_of(expected, "a"), DockZone::Center}));
    EXPECT_EQ(space(entity).layout, expected);
}

TEST_F(DockSpaceTest, ShortTabTravelIsAClickNotADrag) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const DockLayout before = space(entity).layout;
    const glm::vec2 p = center(tab(entity, "c"));

    press(p);
    drag_to(p + glm::vec2{2.0f, 1.0f});
    EXPECT_EQ(runtime(entity).gesture.kind, engine::ui::DockGestureKind::TabPress);
    release(p + glm::vec2{2.0f, 1.0f});
    EXPECT_EQ(space(entity).layout, before);
}

TEST_F(DockSpaceTest, TabDroppedOutsideTheAreaFloatsInsideIt) {
    const Rect area{0.0f, 0.0f, 600.0f, 600.0f};
    const engine::ecs::Entity entity = make_space(side_by_side(), area);
    DockLayout expected = space(entity).layout;
    const Rect c_tab = tab(entity, "c");
    const glm::vec2 p = center(c_tab);
    const engine::ui::DockMetrics& m = space(entity).metrics;
    const glm::vec2 grab{p.x - c_tab.x + m.frame_border, p.y - c_tab.y + m.frame_border + m.title_bar_height};

    press(p);
    drag_to({700.0f, 300.0f});
    const Rect frame = engine::ui::dock_float_clamped(
            Rect{700.0f - grab.x, 300.0f - grab.y, m.float_size.x, m.float_size.y}, area);
    ASSERT_TRUE(runtime(entity).gesture.drop.has_value());
    EXPECT_EQ(runtime(entity).gesture.drop->kind, engine::ui::DockDropKind::Float);
    EXPECT_EQ(runtime(entity).gesture.drop->preview, frame);
    release({700.0f, 300.0f});

    ASSERT_TRUE(expected.float_panel("c", frame));
    EXPECT_EQ(space(entity).layout, expected);
    EXPECT_EQ(panel('c').order, kBase + 3);
    EXPECT_EQ(panel('c').rect, geometry(entity).panel("c")->content);
}

TEST_F(DockSpaceTest, ShiftFloatsATabInsideTheArea) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const glm::vec2 p = center(tab(entity, "c"));

    key(engine::KeyCode::LShift, true);
    press(p);
    drag_to({600.0f, 300.0f});
    release({600.0f, 300.0f});
    key(engine::KeyCode::LShift, false);
    frame();

    ASSERT_EQ(space(entity).layout.floats().size(), 1u);
    EXPECT_EQ(space(entity).layout.find("c")->float_id, space(entity).layout.floats()[0].id);
}

TEST_F(DockSpaceTest, SplitterDragSetsTheRatio) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const engine::ui::DockSplitterRect splitter = geometry(entity).splitters[0];
    const DockNodeId split = splitter.split;
    const std::uint64_t revision = space(entity).revision;

    press(center(splitter.grab));
    EXPECT_TRUE(consumed());
    drag_to({300.0f, 300.0f});
    const float ratio = engine::ui::dock_split_ratio_at(splitter, {300.0f, 300.0f}, space(entity).metrics);
    EXPECT_FLOAT_EQ(space(entity).layout.node(split)->ratio, ratio);
    EXPECT_EQ(space(entity).revision, revision);
    release({300.0f, 300.0f});
    EXPECT_EQ(space(entity).revision, revision + 1);
    EXPECT_EQ(panel('b').rect, geometry(entity).panel("b")->content);
    EXPECT_LT(panel('c').rect.w, 398.0f);
}

TEST_F(DockSpaceTest, FloatTitleDragMovesTheFloat) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const DockNodeId id = space(entity).layout.floats()[0].id;
    const std::uint64_t revision = space(entity).revision;

    key(engine::KeyCode::LShift, true);
    press({200.0f, 110.0f});
    drag_to({250.0f, 160.0f});
    EXPECT_EQ(space(entity).layout.find_float(id)->rect, (Rect{150.0f, 150.0f, 300.0f, 200.0f}));
    EXPECT_EQ(canvas(runtime(entity).floats.at(id).canvas).rect, (Rect{150.0f, 150.0f, 300.0f, 200.0f}));
    release({250.0f, 160.0f});
    EXPECT_EQ(space(entity).revision, revision + 1);
    EXPECT_EQ(panel('c').rect, geometry(entity).panel("c")->content);
}

TEST_F(DockSpaceTest, FloatMoveStaysInTheAreaAndEscapeCancelsIt) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const DockNodeId id = space(entity).layout.floats()[0].id;
    const DockLayout before = space(entity).layout;

    press({200.0f, 110.0f});
    drag_to({790.0f, 590.0f});
    EXPECT_EQ(space(entity).layout.find_float(id)->rect, (Rect{500.0f, 400.0f, 300.0f, 200.0f}));
    // Over the dock area's right edge band: docking there is previewed.
    ASSERT_TRUE(runtime(entity).gesture.drop.has_value());
    EXPECT_EQ(runtime(entity).gesture.drop->target, (DockTarget{engine::ui::kNoDockNode, DockZone::Right}));

    key(engine::KeyCode::Escape, true);
    frame();
    EXPECT_EQ(space(entity).layout, before);
    EXPECT_EQ(runtime(entity).gesture.kind, engine::ui::DockGestureKind::None);
    EXPECT_EQ(canvas(runtime(entity).preview.canvas).rect, Rect{});
    release({790.0f, 590.0f});
    EXPECT_EQ(space(entity).layout, before);
}

TEST_F(DockSpaceTest, FloatTitleDropDocksTheFloat) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    DockLayout expected = space(entity).layout;
    const DockNodeId id = expected.floats()[0].id;

    press({200.0f, 110.0f});
    drag_to({600.0f, 300.0f});
    release({600.0f, 300.0f});
    ASSERT_TRUE(expected.dock_float(id, {stack_of(expected, "a"), DockZone::Center}));
    EXPECT_EQ(space(entity).layout, expected);
    EXPECT_TRUE(runtime(entity).floats.empty());
    EXPECT_EQ(panel('c').order, kBase + 1);
}

TEST_F(DockSpaceTest, FloatEdgeDragResizes) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const DockNodeId id = space(entity).layout.floats()[0].id;

    press({398.0f, 200.0f});
    drag_to({448.0f, 230.0f});
    release({448.0f, 230.0f});
    EXPECT_EQ(space(entity).layout.find_float(id)->rect, (Rect{100.0f, 100.0f, 350.0f, 200.0f}));
}

TEST_F(DockSpaceTest, PressOnFloatContentRaisesIt) {
    DockLayout layout = docked_and_float();
    layout.float_panel("c", Rect{150.0f, 150.0f, 300.0f, 200.0f});
    const engine::ecs::Entity entity = make_space(std::move(layout));
    const DockNodeId lower = space(entity).layout.floats()[0].id;
    const std::uint64_t revision = space(entity).revision;

    press({110.0f, 250.0f});
    EXPECT_TRUE(consumed());
    EXPECT_EQ(space(entity).layout.floats().back().id, lower);
    EXPECT_EQ(space(entity).revision, revision + 1);
    EXPECT_EQ(runtime(entity).gesture.kind, engine::ui::DockGestureKind::None);
    EXPECT_EQ(panel('b').order, kBase + 5);
    EXPECT_EQ(panel('c').order, kBase + 3);
    release({110.0f, 250.0f});
}

TEST_F(DockSpaceTest, DraggingTheOnlyTabOfAFloatMovesTheFloat) {
    DockLayout layout = docked_and_float();
    layout.move("c", {stack_of(layout, "a"), DockZone::Center});
    const engine::ecs::Entity entity = make_space(std::move(layout));
    const DockNodeId id = space(entity).layout.floats()[0].id;
    const glm::vec2 p = center(tab(entity, "b"));

    key(engine::KeyCode::LShift, true);
    press(p);
    drag_to(p + glm::vec2{30.0f, 20.0f});
    EXPECT_EQ(runtime(entity).gesture.kind, engine::ui::DockGestureKind::FloatMove);
    release(p + glm::vec2{30.0f, 20.0f});
    EXPECT_EQ(space(entity).layout.find_float(id)->rect, (Rect{130.0f, 120.0f, 300.0f, 200.0f}));
}

TEST_F(DockSpaceTest, DockedContentIsLeftToThePanel) {
    const engine::ecs::Entity entity = make_space(side_by_side());

    press({200.0f, 300.0f});
    EXPECT_FALSE(consumed());
    release({200.0f, 300.0f});
    // An empty part of a strip is still chrome.
    press({300.0f, 12.0f});
    EXPECT_TRUE(consumed());
    EXPECT_EQ(runtime(entity).gesture.kind, engine::ui::DockGestureKind::None);
    release({300.0f, 12.0f});
    // Hover over a splitter is consumed too.
    drag_to(center(geometry(entity).splitters[0].grab));
    EXPECT_TRUE(consumed());
}

TEST_F(DockSpaceTest, ACanvasAboveTheChromeTakesThePress) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    UiCanvas cover;
    cover.fit = engine::ui::UiFit::Fixed;
    cover.rect = Rect{0.0f, 0.0f, 800.0f, 30.0f};
    cover.order = 100;
    (void) engine::ui::spawn_canvas(world, cover, *engine::ui::make_document(engine::ui::canvas()));
    const DockLayout before = space(entity).layout;

    press(center(tab(entity, "a")));
    EXPECT_FALSE(consumed());
    EXPECT_EQ(space(entity).layout, before);
}

TEST_F(DockSpaceTest, AreaChangeRelaysOutThePanels) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    space(entity).area = Rect{0.0f, 40.0f, 1000.0f, 500.0f};
    frame();
    const DockGeometry g = geometry(entity);
    EXPECT_EQ(panel('b').rect, g.panel("b")->content);
    EXPECT_EQ(panel('b').rect.x + panel('b').rect.w, 1000.0f);
    EXPECT_EQ(canvas(runtime(entity).docked.canvas).rect, (Rect{0.0f, 40.0f, 1000.0f, 500.0f}));
}

TEST_F(DockSpaceTest, FloatsOutsideAShrunkAreaAreShownInsideIt) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const DockNodeId id = space(entity).layout.floats()[0].id;
    space(entity).area = Rect{0.0f, 0.0f, 300.0f, 250.0f};
    frame();
    // The stored rect stays; the float is shown clamped.
    EXPECT_EQ(space(entity).layout.find_float(id)->rect, (Rect{100.0f, 100.0f, 300.0f, 200.0f}));
    EXPECT_EQ(canvas(runtime(entity).floats.at(id).canvas).rect, (Rect{0.0f, 50.0f, 300.0f, 200.0f}));

    // A move starts from where it is shown; Escape puts the stored rect back.
    key(engine::KeyCode::LShift, true);
    press({150.0f, 60.0f});
    drag_to({140.0f, 70.0f});
    EXPECT_EQ(space(entity).layout.find_float(id)->rect, (Rect{0.0f, 50.0f, 300.0f, 200.0f}));
    key(engine::KeyCode::Escape, true);
    frame();
    EXPECT_EQ(space(entity).layout.find_float(id)->rect, (Rect{100.0f, 100.0f, 300.0f, 200.0f}));
    EXPECT_EQ(space(entity).revision, 0u);
}

TEST_F(DockSpaceTest, HostChangesDoNotBumpTheRevision) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    space(entity).layout.activate("a");
    frame();
    EXPECT_EQ(space(entity).revision, 0u);
    EXPECT_EQ(panel('a').rect, geometry(entity).panel("a")->content);
}

TEST_F(DockSpaceTest, CloseButtonSendsARequest) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    space(entity).panels[0].closable = true;
    frame();
    const Rect a_tab = tab(entity, "a");
    const Rect close = engine::ui::dock_close_button_rect(a_tab, space(entity).close_button_size);
    EXPECT_EQ(close, (Rect{a_tab.x + a_tab.w - 5.0f - 14.0f, 5.0f, 14.0f, 14.0f}));
    EXPECT_EQ(runtime(entity).docked.vm->tabs.get()[0]->close.get(), "block");
    const DockLayout before = space(entity).layout;

    engine::ecs::EventCursor<engine::ui::DockPanelCloseRequested> cursor;
    (void) engine::ecs::EventReader<engine::ui::DockPanelCloseRequested>{world, cursor};
    press(center(close));
    std::vector<std::string> keys;
    for (const auto& event : engine::ecs::EventReader<engine::ui::DockPanelCloseRequested>{world, cursor}) {
        EXPECT_EQ(event.space, entity);
        keys.push_back(event.key);
    }
    EXPECT_EQ(keys, std::vector<std::string>{"a"});
    EXPECT_EQ(space(entity).layout, before);
    EXPECT_TRUE(consumed());
}

TEST_F(DockSpaceTest, AHiddenPanelLosesFocus) {
    DockLayout layout = side_by_side();
    layout.activate("a");
    const engine::ecs::Entity entity = make_space(std::move(layout));
    const engine::ecs::Entity a = panel_canvases[0];
    Element& field = world.get<engine::ui::UiInstance>(a).document.root;
    engine::ui::set_focus(world, kPrimaryWindow, a, &field);

    space(entity).layout.activate("c");
    frame();
    EXPECT_EQ(engine::ui::focused_element(world, kPrimaryWindow), nullptr);
}

TEST_F(DockSpaceTest, RemovingTheSpaceDestroysItsChrome) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const engine::ui::DockSpaceRuntime rt = runtime(entity);
    world.destroy(entity);
    frame();
    EXPECT_FALSE(world.valid(rt.docked.canvas));
    EXPECT_FALSE(world.valid(rt.preview.canvas));
    EXPECT_FALSE(world.valid(rt.floats.begin()->second.canvas));
    EXPECT_TRUE(world.ctx<engine::ui::DockRuntime>().spaces.empty());
}

TEST_F(DockSpaceTest, DefaultThemeLaysTheChromeOutWhereTheGeometrySays) {
    engine::ui::Stylesheet sheet = default_theme();
    const engine::ecs::Entity entity = make_space(side_by_side());
    space(entity).panels[0].closable = true;
    frame();
    const engine::ecs::Entity chrome = runtime(entity).docked.canvas;
    world.get<engine::ui::UiInstance>(chrome).stylesheet = std::move(sheet);
    const Rect a_tab = tab(entity, "a");
    engine::ui::update_pointer_hover(world, a_tab.x + 10.0f, a_tab.y + 10.0f);
    EXPECT_TRUE(consumed());

    Element& root = world.get<engine::ui::UiInstance>(chrome).document.root;
    std::vector<Element*> tabs;
    collect_class(root, "dock-tab-item", tabs);
    ASSERT_EQ(tabs.size(), 3u);
    EXPECT_EQ(tabs[0]->layout_rect, a_tab);
    std::vector<Element*> closes;
    collect_class(root, "dock-tab-close", closes);
    ASSERT_EQ(closes.size(), 3u);
    EXPECT_FALSE(closes[0]->display_none);
    EXPECT_EQ(closes[0]->layout_rect, engine::ui::dock_close_button_rect(a_tab, 14.0f));
    EXPECT_TRUE(closes[1]->display_none);
    std::vector<Element*> splitters;
    collect_class(root, "dock-splitter", splitters);
    ASSERT_EQ(splitters.size(), 1u);
    EXPECT_EQ(splitters[0]->layout_rect, geometry(entity).splitters[0].grab);
}

// A chrome list's rows are absolutely placed: they add nothing to the size the list hugs, and a hit test reaches them
// outside the list's box. Before, a list hugged a column of its rows and clipped hit tests to it, so a tab outside
// that column took no hover, and wind-cli `hit` answered the `dock-splitters` list there.
TEST_F(DockSpaceTest, EveryTabAndSplitterIsHitWhereTheGeometryPutsIt) {
    const engine::ecs::Entity entity = make_space(side_by_side(), Rect{0.0f, 40.0f, 800.0f, 560.0f});
    const engine::ecs::Entity chrome = runtime(entity).docked.canvas;
    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(chrome);
    instance.stylesheet = default_theme();
    engine::ui::apply_layout_style(instance.document.root, &*instance.stylesheet);
    engine::ui::layout(instance.document, canvas(chrome).rect);

    for (const std::string_view key : {"a", "b", "c"}) {
        const glm::vec2 p = center(tab(entity, key));
        const Element* hit = engine::ui::hit_test(instance.document.root, p.x, p.y);
        ASSERT_NE(hit, nullptr) << key;
        EXPECT_NE(std::ranges::find(hit->classes, "dock-tab"), hit->classes.end()) << key;
        EXPECT_EQ(hit->text, std::string("Panel ") + std::string(key));
        // What wind-cli `hit` and the UI Inspector pick.
        const engine::ui::VisualHit visual = engine::ui::hit_test_visual(instance.document.root, p.x, p.y);
        EXPECT_EQ(visual.element, hit) << key;
    }
    const glm::vec2 bar = center(geometry(entity).splitters[0].grab);
    const Element* splitter = engine::ui::hit_test(instance.document.root, bar.x, bar.y);
    ASSERT_NE(splitter, nullptr);
    EXPECT_NE(std::ranges::find(splitter->classes, "dock-splitter"), splitter->classes.end());
}

// The cursor update_cursor() gives the window with the default theme on every chrome canvas, laid out as it stands.
class DockCursorTest : public DockSpaceTest {
protected:
    void theme_chrome(engine::ecs::Entity entity) {
        std::vector<engine::ecs::Entity> chromes{runtime(entity).docked.canvas};
        for (const auto& [id, chrome] : runtime(entity).floats) {
            chromes.push_back(chrome.canvas);
        }
        for (const engine::ecs::Entity chrome : chromes) {
            engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(chrome);
            instance.stylesheet = default_theme();
            engine::ui::apply_layout_style(instance.document.root, &*instance.stylesheet);
            engine::ui::layout(instance.document, canvas(chrome).rect);
        }
    }

    engine::ui::Cursor cursor_at(glm::vec2 p) {
        engine::ui::pointer_for(world, kPrimaryWindow).position = p;
        return engine::ui::update_cursor(world, kPrimaryWindow);
    }
};

TEST_F(DockCursorTest, ASplitterShowsTheResizeCursorOfItsAxisAndKeepsItWhileDragged) {
    // `a` left, `b` over `c` right: one splitter each way.
    DockLayout layout;
    layout.add("a", {});
    layout.add("b", {stack_of(layout, "a"), DockZone::Right});
    layout.add("c", {stack_of(layout, "b"), DockZone::Bottom});
    const engine::ecs::Entity entity = make_space(std::move(layout));
    theme_chrome(entity);
    const DockGeometry g = geometry(entity);
    ASSERT_EQ(g.splitters.size(), 2u);
    for (const engine::ui::DockSplitterRect& s : g.splitters) {
        const engine::ui::Cursor expected = s.axis == engine::ui::DockAxis::Horizontal ? engine::ui::Cursor::EwResize
                                                                                       : engine::ui::Cursor::NsResize;
        EXPECT_EQ(cursor_at(center(s.grab)), expected);
    }
    EXPECT_EQ(cursor_at(center(tab(entity, "a"))), engine::ui::Cursor::Default);
    EXPECT_EQ(cursor_at(center(g.panel("a")->content)), engine::ui::Cursor::Default);

    const auto columns =
            std::ranges::find(g.splitters, engine::ui::DockAxis::Horizontal, &engine::ui::DockSplitterRect::axis);
    ASSERT_NE(columns, g.splitters.end());
    const glm::vec2 grab = center(columns->grab);
    // Straight from a panel to a press on the bar, with no frame resolving the cursor in between.
    EXPECT_EQ(cursor_at(center(g.panel("a")->content)), engine::ui::Cursor::Default);
    send(MouseEvent::Kind::Move, grab);
    press(grab);
    EXPECT_EQ(cursor_at(grab), engine::ui::Cursor::EwResize);
    // Off the bar, over a panel, while the button is down.
    const glm::vec2 over_a = grab - glm::vec2{60.0f, 0.0f};
    drag_to(over_a);
    EXPECT_EQ(cursor_at(over_a), engine::ui::Cursor::EwResize);
    release(over_a);
    EXPECT_EQ(cursor_at(over_a), engine::ui::Cursor::Default);
}

TEST_F(DockCursorTest, AFloatsEdgesShowTheResizeCursorOfTheirSideAndItsTitleMove) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    theme_chrome(entity);
    const DockGeometry g = geometry(entity);
    ASSERT_EQ(g.floats.size(), 1u);
    // The float's frame is (100, 100) 300 x 200 with a 4px band.
    const Rect f = g.floats[0].frame;
    ASSERT_EQ(f, (Rect{100.0f, 100.0f, 300.0f, 200.0f}));
    const float l = f.x + 1.0f;
    const float r = f.x + f.w - 1.0f;
    const float t = f.y + 1.0f;
    const float b = f.y + f.h - 1.0f;
    const float mx = f.x + f.w * 0.5f;
    const float my = f.y + f.h * 0.5f;
    const std::pair<glm::vec2, engine::ui::Cursor> cases[] = {
            {{l, my}, engine::ui::Cursor::EwResize},   {{r, my}, engine::ui::Cursor::EwResize},
            {{mx, t}, engine::ui::Cursor::NsResize},   {{mx, b}, engine::ui::Cursor::NsResize},
            {{l, t}, engine::ui::Cursor::NwseResize},  {{r, b}, engine::ui::Cursor::NwseResize},
            {{r, t}, engine::ui::Cursor::NeswResize},  {{l, b}, engine::ui::Cursor::NeswResize},
    };
    const engine::ui::DockMetrics metrics = engine::ui::dock_space_metrics(space(entity), runtime(entity));
    for (const auto& [p, expected] : cases) {
        EXPECT_EQ(cursor_at(p), expected) << p.x << ", " << p.y;
        // Where the dock input starts a resize of the same edges.
        const std::optional<engine::ui::DockChromeHit> hit = engine::ui::dock_chrome_at(g, metrics, p);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->kind, engine::ui::DockChromeKind::FloatEdge);
    }
    EXPECT_EQ(cursor_at(center(g.floats[0].title)), engine::ui::Cursor::Move);
    EXPECT_EQ(cursor_at(center(g.panel("b")->content)), engine::ui::Cursor::Default);
}

TEST_F(DockSpaceTest, WithoutAPainterEveryTabIsTabWidth) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    world.get<engine::ui::UiInstance>(runtime(entity).docked.canvas).stylesheet = default_theme();
    frame();
    EXPECT_TRUE(runtime(entity).tabs.by_key.empty());
    EXPECT_EQ(tab(entity, "a").w, 120.0f);
    EXPECT_EQ(tab(entity, "b").w, 120.0f);
}

TEST_F(DockSpaceTest, TabsAreAsWideAsTheirTitlesInTheThemesFontAndPadding) {
    HalfEmPainter painter;
    world.ctx<engine::ui::UiLayoutPainters>().resolve = [&](engine::WindowId) { return &painter; };
    const engine::ecs::Entity entity = make_space(side_by_side());
    // No chrome stylesheet loaded yet: nothing to measure with.
    EXPECT_EQ(tab(entity, "a").w, 120.0f);

    space(entity).panels[0].closable = true;
    space(entity).panels[1].title = "Inspector";
    world.get<engine::ui::UiInstance>(runtime(entity).docked.canvas).stylesheet = default_theme();
    frame();
    // dock.css: 12px font, 8px padding each side. "Inspector" is 9 x 6 = 54 wide.
    EXPECT_EQ(tab(entity, "b").w, 54.0f + 16.0f);
    // A closable tab adds the close button and its gap to the tab's right edge: 14 + (24 - 14) / 2.
    EXPECT_EQ(tab(entity, "a").w, 42.0f + 16.0f + 19.0f);
    EXPECT_EQ(tab(entity, "c").w, 42.0f + 16.0f);
    ASSERT_FALSE(painter.fonts.empty());
    EXPECT_EQ(painter.fonts.front(), engine::builtin::font_ui);

    // The close button sits in the room the padding keeps: the title's box ends before it.
    engine::ui::UiInstance& chrome = world.get<engine::ui::UiInstance>(runtime(entity).docked.canvas);
    std::vector<Element*> tabs;
    collect_class(chrome.document.root, "dock-tab", tabs);
    ASSERT_EQ(tabs.size(), 3u);
    engine::ui::apply_layout_style(chrome.document.root, &*chrome.stylesheet);
    const Rect a = tab(entity, "a");
    const float title_right = a.x + a.w - engine::ui::resolve_length(tabs[0]->padding.right, 0.0f, 12.0f);
    EXPECT_LE(title_right, engine::ui::dock_close_button_rect(a, 14.0f).x - 8.0f + 0.001f);

    // A title change measures that tab again.
    painter.fonts.clear();
    space(entity).panels[2].title = "C";
    frame();
    EXPECT_EQ(tab(entity, "c").w, 6.0f + 16.0f);
    EXPECT_EQ(painter.fonts.size(), 1u) << "only the changed tab is measured";
}

TEST_F(DockSpaceTest, AHostTabWidthWinsOverTheMeasuredOne) {
    HalfEmPainter painter;
    world.ctx<engine::ui::UiLayoutPainters>().resolve = [&](engine::WindowId) { return &painter; };
    const engine::ecs::Entity entity = make_space(side_by_side());
    world.get<engine::ui::UiInstance>(runtime(entity).docked.canvas).stylesheet = default_theme();
    space(entity).metrics.tab_width_for = [](std::string_view key) { return key == "a" ? 90.0f : 30.0f; };
    frame();
    EXPECT_EQ(tab(entity, "a").w, 90.0f);
    EXPECT_EQ(tab(entity, "b").w, 30.0f);
}

} // namespace
