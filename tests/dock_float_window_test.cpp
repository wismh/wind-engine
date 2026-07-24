#include <gtest/gtest.h>

#include "fixtures/fake_services.h"
#include "ui/dock_runtime.h"

#include <engine/core/input_system.h>
#include <engine/core/worlds.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/dock_space.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>

#include <string>
#include <string_view>
#include <vector>

// engine/ui/dock_space.h, DockFloatMode::OsWindow: each float lives in an OS window of its own, opened, moved, and
// closed by the dock systems through IWindowControl. docs/tech/features/Docking.md#os-window-floats.

namespace {

using engine::kPrimaryWindow;
using engine::MouseButton;
using engine::MouseEvent;
using engine::WindowId;
using engine::render::Rect;
using engine::ui::DockFloatMode;
using engine::ui::DockLayout;
using engine::ui::DockNodeId;
using engine::ui::DockSpace;
using engine::ui::DockTarget;
using engine::ui::DockZone;
using engine::ui::UiCanvas;

constexpr int kBase = 10;
// The space's window on screen: client area at (100, 50), 800 x 600.
constexpr glm::ivec2 kOrigin{100, 50};
constexpr Rect kArea{0.0f, 0.0f, 800.0f, 600.0f};

glm::vec2 center(const Rect& r) {
    return {r.x + r.w * 0.5f, r.y + r.h * 0.5f};
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

// `a` docked; `b` and `c` tabbed in one float at (100, 100) 300 x 200, `c` active.
DockLayout docked_and_float() {
    DockLayout layout;
    layout.add("a", {});
    layout.add("b", {stack_of(layout, "a"), DockZone::Right});
    layout.float_panel("b", Rect{100.0f, 100.0f, 300.0f, 200.0f});
    layout.add("c", {stack_of(layout, "b"), DockZone::Center});
    return layout;
}

// A process with one world bound to kPrimaryWindow, the engine systems registered with the fake window control.
class DockFloatWindowTest : public ::testing::Test {
protected:
    fakes::QuietFatal fatal;
    fakes::FakeWindowControl windows;
    engine::render::CommandBuffer commands;
    engine::Worlds worlds{fatal};
    engine::ecs::World* world = nullptr;
    std::vector<engine::ecs::Entity> panel_canvases;

    void SetUp() override {
        worlds.set_deps(engine::EngineSystemDeps{.commands = &commands, .windows = &windows, .worlds = &worlds});
        world = &worlds.add();
        worlds.bind_window(kPrimaryWindow, *world);
        worlds.enable_ui(*world);
        worlds.presentation().sizes.sizes[kPrimaryWindow] = engine::ui::WindowSize{800, 600};
        windows.positions[kPrimaryWindow] = kOrigin;
        windows.sizes[kPrimaryWindow] = {800, 600};
    }

    engine::ecs::Entity make_space(DockLayout layout, DockFloatMode mode = DockFloatMode::OsWindow) {
        DockSpace space;
        space.area = kArea;
        space.order = kBase;
        space.float_mode = mode;
        space.layout = std::move(layout);
        for (const char* key : {"a", "b", "c"}) {
            UiCanvas canvas;
            canvas.fit = engine::ui::UiFit::FillWindow;
            canvas.order = 500;
            const engine::ecs::Entity entity =
                    engine::ui::spawn_canvas(*world, canvas, *engine::ui::make_document(engine::ui::canvas()));
            panel_canvases.push_back(entity);
            space.panels.push_back(engine::ui::DockPanel{key, std::string("Panel ") + key, entity});
        }
        const engine::ecs::Entity entity = world->create();
        world->emplace<DockSpace>(entity, std::move(space));
        frame();
        return entity;
    }

    void frame() {
        engine::ui::reset_pointer_frame(worlds.presentation());
        world->run(engine::ecs::Schedule::Frame);
        world->flush_events();
    }

    void send(WindowId window, MouseEvent::Kind kind, glm::vec2 p) {
        engine::ecs::EventWriter<MouseEvent>{*world}.send(MouseEvent{
                .window = window,
                .kind = kind,
                .position = p,
                .button = kind == MouseEvent::Kind::Move ? MouseButton::None : MouseButton::Left,
        });
    }

    void press(WindowId window, glm::vec2 p) {
        send(window, MouseEvent::Kind::Down, p);
        frame();
    }

    void drag_to(WindowId window, glm::vec2 p) {
        send(window, MouseEvent::Kind::Move, p);
        frame();
    }

    void release(WindowId window, glm::vec2 p) {
        send(window, MouseEvent::Kind::Up, p);
        frame();
    }

    void key(WindowId window, engine::KeyCode code, bool down) {
        engine::ecs::EventWriter<engine::KeyEvent>{*world}.send(
                engine::KeyEvent{.window = window, .key = code, .down = down});
    }

    DockSpace& space(engine::ecs::Entity entity) { return world->get<DockSpace>(entity); }

    UiCanvas& canvas(engine::ecs::Entity entity) { return world->get<UiCanvas>(entity); }

    UiCanvas& panel(char key) { return canvas(panel_canvases[static_cast<std::size_t>(key - 'a')]); }

    engine::ui::DockSpaceRuntime& runtime(engine::ecs::Entity entity) {
        return world->ctx<engine::ui::DockRuntime>().spaces.at(entity);
    }

    // The OS window of the float that holds `key`.
    WindowId window_of(engine::ecs::Entity entity, std::string_view key) {
        const std::optional<WindowId> window =
                engine::ui::dock_float_window(runtime(entity), space(entity).layout.find(key)->float_id);
        EXPECT_TRUE(window.has_value()) << key;
        return window.value_or(kPrimaryWindow);
    }

    // A tab's rect in the window it is shown in.
    Rect tab(engine::ecs::Entity entity, std::string_view key) {
        const DockLayout& layout = space(entity).layout;
        const DockNodeId float_id = layout.find(key)->float_id;
        const engine::ui::DockGeometry g = engine::ui::dock_float_window(runtime(entity), float_id)
                ? engine::ui::dock_float_window_geometry(space(entity), runtime(entity), float_id)
                : engine::ui::dock_space_geometry(space(entity), runtime(entity));
        const engine::ui::DockStackRect* stack = g.stack(stack_of(layout, key));
        return std::ranges::find(stack->tabs, key, &engine::ui::DockTabRect::key)->rect;
    }

    bool bound_here(WindowId window) const { return worlds.world_for(window) == world; }
};

TEST_F(DockFloatWindowTest, AFloatOpensAWindowBoundToTheWorldWithItsTitleAndRect) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    ASSERT_EQ(windows.open.size(), 2u);
    const WindowId window = windows.open[1];
    EXPECT_EQ(window_of(entity, "b"), window);
    EXPECT_TRUE(bound_here(window));
    const engine::WindowDesc& desc = windows.opened.at(window);
    EXPECT_EQ(desc.title, "Panel c");
    EXPECT_EQ(desc.size, (glm::ivec2{300, 200}));
    EXPECT_EQ(desc.position, (glm::ivec2{200, 150}));
    EXPECT_EQ(desc.owner, kPrimaryWindow) << "a tool window of the space's window";
    EXPECT_TRUE(desc.style.utility) << "no taskbar entry of its own";
    EXPECT_EQ(space(entity).revision, 0u) << "opening a window changes no layout";
    EXPECT_EQ(engine::ui::dock_panel_os_window(*world, entity, "b"), window);
    EXPECT_EQ(engine::ui::dock_panel_os_window(*world, entity, "c"), window);
    EXPECT_EQ(engine::ui::dock_panel_os_window(*world, entity, "a"), std::nullopt) << "docked";
    EXPECT_EQ(engine::ui::dock_panel_os_window(*world, entity, "x"), std::nullopt) << "unknown";
    EXPECT_EQ(engine::ui::dock_panel_os_window(*world, panel_canvases[0], "b"), std::nullopt) << "no DockSpace";

    // The float's tabs fill the client area; the OS frame and title bar replace the virtual ones.
    EXPECT_EQ(panel('c').window, window);
    EXPECT_EQ(panel('c').rect, (Rect{0.0f, 24.0f, 300.0f, 176.0f}));
    EXPECT_EQ(panel('c').order, kBase + 3);
    EXPECT_EQ(panel('b').window, window);
    EXPECT_EQ(panel('b').rect, Rect{});
    EXPECT_EQ(panel('a').window, kPrimaryWindow);
    const engine::ui::DockSpaceRuntime& rt = runtime(entity);
    const UiCanvas& chrome = canvas(rt.floats.begin()->second.canvas);
    EXPECT_EQ(chrome.window, window);
    EXPECT_EQ(chrome.rect, (Rect{0.0f, 0.0f, 300.0f, 200.0f}));
    EXPECT_TRUE(rt.floats.begin()->second.vm->frames.get().empty());
    EXPECT_TRUE(rt.floats.begin()->second.vm->titles.get().empty());
    ASSERT_EQ(rt.floats.begin()->second.vm->tabs.get().size(), 2u);
    // The space's own window shows only the docked tree.
    EXPECT_TRUE(engine::ui::dock_space_geometry(space(entity), rt).floats.empty());

    // A new active tab retitles the window.
    space(entity).layout.activate("b");
    frame();
    EXPECT_EQ(windows.titles.at(window), "Panel b");
}

TEST_F(DockFloatWindowTest, SwitchingTheModeConvertsTheFloats) {
    const engine::ecs::Entity entity = make_space(docked_and_float(), DockFloatMode::Virtual);
    EXPECT_EQ(windows.open.size(), 1u);
    EXPECT_EQ(panel('c').window, kPrimaryWindow);
    const DockLayout before = space(entity).layout;

    space(entity).float_mode = DockFloatMode::OsWindow;
    frame();
    ASSERT_EQ(windows.open.size(), 2u);
    const WindowId window = windows.open[1];
    EXPECT_EQ(panel('c').window, window);
    EXPECT_EQ(windows.opened.at(window).position, (glm::ivec2{200, 150}));

    space(entity).float_mode = DockFloatMode::Virtual;
    frame();
    EXPECT_EQ(windows.open, std::vector<WindowId>{kPrimaryWindow});
    EXPECT_FALSE(bound_here(window));
    EXPECT_EQ(panel('c').window, kPrimaryWindow);
    const engine::ui::DockGeometry g = engine::ui::dock_space_geometry(space(entity), runtime(entity));
    EXPECT_EQ(panel('c').rect, g.panel("c")->content);
    ASSERT_EQ(g.floats.size(), 1u);
    EXPECT_EQ(g.floats[0].frame, (Rect{100.0f, 100.0f, 300.0f, 200.0f}));
    EXPECT_EQ(space(entity).layout, before) << "a switch keeps the rects";
}

TEST_F(DockFloatWindowTest, ATabDroppedOutsideTheWindowOpensAWindowAtThePointer) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const Rect c_tab = tab(entity, "c");
    const glm::vec2 p = center(c_tab);
    const glm::vec2 grab = p - glm::vec2{c_tab.x, c_tab.y};
    const std::uint64_t revision = space(entity).revision;

    press(kPrimaryWindow, p);
    // Past the window's right edge: the OS keeps sending the pointer to the window that got the press.
    drag_to(kPrimaryWindow, {900.0f, 300.0f});
    const engine::ui::DockGesture& gesture = runtime(entity).gesture;
    ASSERT_TRUE(gesture.drop.has_value());
    EXPECT_EQ(gesture.drop->kind, engine::ui::DockDropKind::Float);
    EXPECT_FALSE(gesture.preview_window.has_value()) << "no window to preview in";
    EXPECT_EQ(canvas(runtime(entity).preview.canvas).rect, Rect{});
    release(kPrimaryWindow, {900.0f, 300.0f});

    const Rect rect{900.0f - grab.x, 300.0f - grab.y, 320.0f, 240.0f};
    DockLayout expected = side_by_side();
    ASSERT_TRUE(expected.float_panel("c", rect));
    EXPECT_EQ(space(entity).layout, expected);
    EXPECT_EQ(space(entity).revision, revision + 1);
    ASSERT_EQ(windows.open.size(), 2u);
    const WindowId window = windows.open[1];
    EXPECT_TRUE(bound_here(window));
    EXPECT_EQ(windows.opened.at(window).title, "Panel c");
    EXPECT_EQ(windows.opened.at(window).size, (glm::ivec2{320, 240}));
    EXPECT_EQ(windows.opened.at(window).position,
            (glm::ivec2{kOrigin.x + static_cast<int>(rect.x), kOrigin.y + static_cast<int>(rect.y)}));
    EXPECT_EQ(panel('c').window, window);
    EXPECT_EQ(panel('c').rect, (Rect{0.0f, 24.0f, 320.0f, 216.0f}));
    EXPECT_EQ(panel('a').window, kPrimaryWindow);
}

TEST_F(DockFloatWindowTest, ShiftFloatsATabIntoAWindowOverTheDockArea) {
    const engine::ecs::Entity entity = make_space(side_by_side());
    const glm::vec2 p = center(tab(entity, "c"));

    key(kPrimaryWindow, engine::KeyCode::LShift, true);
    press(kPrimaryWindow, p);
    drag_to(kPrimaryWindow, {600.0f, 300.0f});
    ASSERT_TRUE(runtime(entity).gesture.drop.has_value());
    EXPECT_EQ(runtime(entity).gesture.drop->kind, engine::ui::DockDropKind::Float);
    release(kPrimaryWindow, {600.0f, 300.0f});

    ASSERT_EQ(space(entity).layout.floats().size(), 1u);
    EXPECT_EQ(windows.open.size(), 2u);
    EXPECT_EQ(panel('c').window, windows.open[1]);
}

TEST_F(DockFloatWindowTest, ATabDraggedFromAFloatWindowDocksInTheMainWindow) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const WindowId window = window_of(entity, "b");
    const DockNodeId a_stack = stack_of(space(entity).layout, "a");

    // Window client (0..300, 0..200) sits at screen (200, 150). The pointer goes to screen (800, 350): main window
    // client (700, 300), the right edge band of `a`.
    press(window, center(tab(entity, "b")));
    EXPECT_TRUE(engine::ui::presentation_of(*world).mouse.consumed_for(window));
    drag_to(window, {600.0f, 200.0f});
    const engine::ui::DockGesture& gesture = runtime(entity).gesture;
    ASSERT_TRUE(gesture.drop.has_value());
    EXPECT_EQ(gesture.drop->target, (DockTarget{a_stack, DockZone::Right}));
    ASSERT_EQ(gesture.preview_window, kPrimaryWindow);
    const UiCanvas& preview = canvas(runtime(entity).preview.canvas);
    EXPECT_EQ(preview.window, kPrimaryWindow);
    EXPECT_EQ(preview.rect, gesture.drop->preview);
    release(window, {600.0f, 200.0f});

    EXPECT_EQ(space(entity).layout.find("b")->float_id, engine::ui::kNoDockNode);
    EXPECT_EQ(panel('b').window, kPrimaryWindow);
    EXPECT_TRUE(windows.is_open(window)) << "`c` is still in it";
    EXPECT_EQ(panel('c').window, window);
    EXPECT_EQ(canvas(runtime(entity).preview.canvas).rect, Rect{});

    // The last tab out moves its panel, and the empty float's window closes.
    press(window, center(tab(entity, "c")));
    drag_to(window, {200.0f, 400.0f});
    ASSERT_TRUE(runtime(entity).gesture.drop.has_value());
    release(window, {200.0f, 400.0f});
    EXPECT_TRUE(space(entity).layout.floats().empty());
    EXPECT_EQ(panel('c').window, kPrimaryWindow);
    EXPECT_FALSE(windows.is_open(window));
    EXPECT_FALSE(bound_here(window));
}

TEST_F(DockFloatWindowTest, EscapeInTheWindowOfTheDragCancelsIt) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const WindowId window = window_of(entity, "b");
    const DockLayout before = space(entity).layout;
    press(window, center(tab(entity, "c")));
    drag_to(window, {600.0f, 200.0f});
    ASSERT_TRUE(runtime(entity).gesture.drop.has_value());

    key(window, engine::KeyCode::Escape, true);
    frame();
    EXPECT_EQ(runtime(entity).gesture.kind, engine::ui::DockGestureKind::None);
    EXPECT_EQ(canvas(runtime(entity).preview.canvas).rect, Rect{});
    release(window, {600.0f, 200.0f});
    EXPECT_EQ(space(entity).layout, before);
}

TEST_F(DockFloatWindowTest, ATabDraggedOntoAnotherFloatWindowJoinsItsStack) {
    DockLayout layout = docked_and_float();
    layout.float_panel("c", Rect{500.0f, 100.0f, 300.0f, 200.0f});
    const engine::ecs::Entity entity = make_space(std::move(layout));
    const WindowId from = window_of(entity, "c");
    const WindowId to = window_of(entity, "b");
    ASSERT_NE(from, to);
    // `b`'s window is at screen (200, 150); `c`'s at (600, 150). The pointer goes to `b`'s strip, screen (300, 160).
    press(from, center(tab(entity, "c")));
    drag_to(from, {-300.0f, 10.0f});
    const engine::ui::DockGesture& gesture = runtime(entity).gesture;
    ASSERT_TRUE(gesture.drop.has_value());
    EXPECT_EQ(gesture.drop->target, (DockTarget{stack_of(space(entity).layout, "b"), DockZone::Center}));
    EXPECT_EQ(gesture.preview_window, to);
    EXPECT_EQ(canvas(runtime(entity).preview.canvas).window, to);
    release(from, {-300.0f, 10.0f});

    EXPECT_EQ(space(entity).layout.node(stack_of(space(entity).layout, "b"))->panels,
            (std::vector<std::string>{"b", "c"}));
    EXPECT_EQ(panel('c').window, to);
    EXPECT_FALSE(windows.is_open(from));
    EXPECT_TRUE(windows.is_open(to));
}

TEST_F(DockFloatWindowTest, ASplitterInsideAFloatWindowSetsTheRatio) {
    DockLayout layout = docked_and_float();
    layout.move("c", {stack_of(layout, "b"), DockZone::Right});
    const engine::ecs::Entity entity = make_space(std::move(layout));
    const WindowId window = window_of(entity, "b");
    const DockNodeId float_id = space(entity).layout.find("b")->float_id;
    const engine::ui::DockGeometry g = engine::ui::dock_float_window_geometry(space(entity), runtime(entity), float_id);
    ASSERT_EQ(g.splitters.size(), 1u);
    const engine::ui::DockSplitterRect splitter = g.splitters[0];

    press(window, center(splitter.grab));
    drag_to(window, {100.0f, 100.0f});
    release(window, {100.0f, 100.0f});
    EXPECT_FLOAT_EQ(space(entity).layout.node(splitter.split)->ratio,
            engine::ui::dock_split_ratio_at(splitter, {100.0f, 100.0f}, space(entity).metrics));
    EXPECT_EQ(panel('b').rect.x, 0.0f);
    EXPECT_LT(panel('b').rect.w, 149.0f);
}

TEST_F(DockFloatWindowTest, PanelContentInAFloatWindowIsLeftToThePanel) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const WindowId window = window_of(entity, "b");
    press(window, {150.0f, 120.0f});
    EXPECT_FALSE(engine::ui::presentation_of(*world).mouse.consumed_for(window));
    EXPECT_EQ(runtime(entity).gesture.kind, engine::ui::DockGestureKind::None);
    release(window, {150.0f, 120.0f});
}

TEST_F(DockFloatWindowTest, NativeMovesAndResizesUpdateTheRectAndLayoutRectsMoveTheWindow) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const WindowId window = window_of(entity, "b");
    const DockNodeId float_id = space(entity).layout.floats()[0].id;
    const std::uint64_t revision = space(entity).revision;

    windows.positions[window] = {250, 170};
    windows.sizes[window] = {400, 300};
    frame();
    EXPECT_EQ(space(entity).layout.find_float(float_id)->rect, (Rect{150.0f, 120.0f, 400.0f, 300.0f}));
    EXPECT_EQ(space(entity).revision, revision + 1);
    EXPECT_EQ(panel('c').rect, (Rect{0.0f, 24.0f, 400.0f, 276.0f}));
    frame();
    EXPECT_EQ(space(entity).revision, revision + 1) << "only a change bumps it";

    // The main window moving does not move the float.
    windows.positions[kPrimaryWindow] = {0, 0};
    frame();
    EXPECT_EQ(space(entity).revision, revision + 1);
    EXPECT_EQ(windows.positions.at(window), (glm::ivec2{250, 170}));

    // A host change of the rect moves and resizes the window.
    space(entity).layout.set_float_rect(float_id, Rect{10.0f, 20.0f, 330.0f, 220.0f});
    frame();
    EXPECT_EQ(windows.positions.at(window), (glm::ivec2{10, 20}));
    EXPECT_EQ(windows.sizes.at(window), (glm::ivec2{330, 220}));
    EXPECT_EQ(space(entity).revision, revision + 1);
}

TEST_F(DockFloatWindowTest, TheCloseButtonDocksTheFloatBackAndClosesTheWindow) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const WindowId window = window_of(entity, "b");
    const std::uint64_t revision = space(entity).revision;
    DockLayout expected = space(entity).layout;
    ASSERT_TRUE(expected.dock_float(expected.floats()[0].id, {stack_of(expected, "a"), DockZone::Center}));

    engine::ecs::EventWriter<engine::ui::WindowCloseRequestedEvent>{*world}.send(
            engine::ui::WindowCloseRequestedEvent{.window = window});
    frame();
    EXPECT_EQ(space(entity).layout, expected);
    EXPECT_TRUE(space(entity).layout.is_visible("c")) << "its active tab stays active";
    EXPECT_EQ(space(entity).revision, revision + 1);
    EXPECT_FALSE(windows.is_open(window));
    EXPECT_FALSE(bound_here(window));
    EXPECT_EQ(panel('c').window, kPrimaryWindow);
    EXPECT_EQ(panel('b').window, kPrimaryWindow);
}

TEST_F(DockFloatWindowTest, AClosedFloatWithSplitsGoesToTheRightOfTheDockArea) {
    DockLayout layout = docked_and_float();
    layout.move("c", {stack_of(layout, "b"), DockZone::Bottom});
    const engine::ecs::Entity entity = make_space(std::move(layout));
    DockLayout expected = space(entity).layout;
    ASSERT_TRUE(expected.dock_float(expected.floats()[0].id, {engine::ui::kNoDockNode, DockZone::Right}));

    engine::ecs::EventWriter<engine::ui::WindowCloseRequestedEvent>{*world}.send(
            engine::ui::WindowCloseRequestedEvent{.window = window_of(entity, "b")});
    frame();
    EXPECT_EQ(space(entity).layout, expected);
}

TEST_F(DockFloatWindowTest, RemovingTheSpaceClosesItsWindows) {
    DockLayout layout = docked_and_float();
    layout.float_panel("c", Rect{500.0f, 100.0f, 300.0f, 200.0f});
    const engine::ecs::Entity entity = make_space(std::move(layout));
    ASSERT_EQ(windows.open.size(), 3u);
    const std::vector<WindowId> floats{windows.open[1], windows.open[2]};

    world->destroy(entity);
    frame();
    EXPECT_EQ(windows.open, std::vector<WindowId>{kPrimaryWindow});
    for (const WindowId window : floats) {
        EXPECT_FALSE(bound_here(window));
    }
}

TEST_F(DockFloatWindowTest, DestroyingTheWorldClosesItsFloatWindows) {
    DockLayout layout = docked_and_float();
    layout.float_panel("c", Rect{500.0f, 100.0f, 300.0f, 200.0f});
    (void) make_space(std::move(layout));
    ASSERT_EQ(windows.open.size(), 3u);
    const std::vector<WindowId> floats{windows.open[1], windows.open[2]};
    std::vector<std::string> log;
    windows.log = &log;

    worlds.destroy(*world);
    world = nullptr;
    windows.log = nullptr;
    EXPECT_EQ(windows.open, std::vector<WindowId>{kPrimaryWindow}) << "the world's own window stays open";
    for (const WindowId window : floats) {
        EXPECT_EQ(worlds.world_for(window), nullptr);
    }
    EXPECT_EQ(log, (std::vector<std::string>{"windows.close 1", "windows.close 2"}));
}

TEST_F(DockFloatWindowTest, DestroyingAnotherWorldLeavesTheseFloatWindowsOpen) {
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const WindowId window = window_of(entity, "b");
    engine::ecs::World& other = worlds.add();
    worlds.enable_ui(other);

    worlds.destroy(other);
    EXPECT_TRUE(windows.is_open(window));
    EXPECT_TRUE(bound_here(window));
}

TEST_F(DockFloatWindowTest, ClosingTheSpaceWindowClosesItsFloatWindowsAndTheFloatsTurnVirtual) {
    // The float windows are owned by the space's window: closing it closes them. A new window cannot be owned by a
    // closed one, so the floats stay virtual.
    const engine::ecs::Entity entity = make_space(docked_and_float());
    const WindowId window = window_of(entity, "b");
    windows.close_window(kPrimaryWindow);
    EXPECT_FALSE(windows.is_open(window));
    frame();
    EXPECT_TRUE(runtime(entity).windows.empty());
    EXPECT_TRUE(runtime(entity).windows_failed);
    EXPECT_FALSE(bound_here(window));
}

TEST_F(DockFloatWindowTest, AWindowThatDoesNotOpenLeavesTheFloatsVirtual) {
    windows.refuse_open = true;
    const engine::ecs::Entity entity = make_space(docked_and_float());
    EXPECT_TRUE(runtime(entity).windows.empty());
    EXPECT_TRUE(runtime(entity).windows_failed);
    EXPECT_EQ(panel('c').window, kPrimaryWindow);
    const engine::ui::DockGeometry g = engine::ui::dock_space_geometry(space(entity), runtime(entity));
    ASSERT_EQ(g.floats.size(), 1u);
    EXPECT_EQ(panel('c').rect, g.panel("c")->content);
    EXPECT_EQ(runtime(entity).floats.begin()->second.vm->frames.get().size(), 1u);
}

TEST_F(DockFloatWindowTest, ASavedFloatOpensAtItsRectAgain) {
    const engine::ecs::Entity first = make_space(docked_and_float());
    const WindowId window = window_of(first, "b");
    windows.positions[window] = {420, 260};
    windows.sizes[window] = {360, 280};
    frame();
    const std::string text = engine::ui::dock_layout_to_text(space(first).layout);
    world->destroy(first);
    frame();
    ASSERT_EQ(windows.open.size(), 1u);

    const std::optional<DockLayout> saved = engine::ui::dock_layout_from_text(text);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->floats()[0].rect, (Rect{320.0f, 210.0f, 360.0f, 280.0f}));
    panel_canvases.clear();
    const engine::ecs::Entity second = make_space(*saved);
    ASSERT_EQ(windows.open.size(), 2u);
    const engine::WindowDesc& desc = windows.opened.at(windows.open[1]);
    EXPECT_EQ(desc.position, (glm::ivec2{420, 260}));
    EXPECT_EQ(desc.size, (glm::ivec2{360, 280}));
    EXPECT_EQ(space(second).revision, 0u);
}

TEST_F(DockFloatWindowTest, AFloatOffEveryDisplayOpensOnTheSpacesDisplay) {
    windows.displays = {Rect{0.0f, 0.0f, 1920.0f, 1040.0f}};
    DockLayout layout = docked_and_float();
    layout.set_float_rect(layout.floats()[0].id, Rect{4000.0f, 3000.0f, 300.0f, 200.0f});
    const engine::ecs::Entity entity = make_space(std::move(layout));
    ASSERT_EQ(windows.open.size(), 2u);
    const engine::WindowDesc& desc = windows.opened.at(windows.open[1]);
    EXPECT_EQ(desc.position, (glm::ivec2{1620, 840}));
    // The layout follows where the window is.
    EXPECT_EQ(space(entity).layout.floats()[0].rect, (Rect{1520.0f, 790.0f, 300.0f, 200.0f}));
    EXPECT_EQ(space(entity).revision, 1u);
}

TEST_F(DockFloatWindowTest, WithoutWindowControlFloatsStayVirtual) {
    engine::render::CommandBuffer own;
    engine::ecs::World bare;
    engine::register_engine_systems(bare, engine::EngineSystemDeps{.commands = &own});
    engine::ui::presentation_of(bare).sizes.sizes[kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    DockSpace space;
    space.area = kArea;
    space.float_mode = DockFloatMode::OsWindow;
    space.layout = docked_and_float();
    const engine::ecs::Entity entity = bare.create();
    bare.emplace<DockSpace>(entity, std::move(space));
    bare.run(engine::ecs::Schedule::Frame);
    EXPECT_TRUE(bare.ctx<engine::ui::DockRuntime>().spaces.at(entity).windows.empty());
    EXPECT_EQ(windows.open.size(), 1u);
    const engine::ui::DockGeometry g = engine::ui::dock_space_geometry(
            bare.get<DockSpace>(entity), bare.ctx<engine::ui::DockRuntime>().spaces.at(entity));
    EXPECT_EQ(g.floats.size(), 1u);
}

} // namespace
