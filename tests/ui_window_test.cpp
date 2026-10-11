#include <gtest/gtest.h>

#include <engine/core/bound_windows.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/ui_window.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fixtures/fake_services.h"
#include "ui/painter.h"

namespace {

using engine::UiWindow;

// A window control that looks at the world at the moment a window closes: the teardown order shows in what is
// already gone by then.
class WatchedWindows final : public fakes::FakeWindowControl {
public:
    void close_window(engine::WindowId id) override {
        ++close_calls;
        if (world != nullptr) {
            canvas_count_at_close = 0;
            auto canvases = world->view<engine::ui::UiCanvas>();
            for (const engine::ecs::Entity entity : canvases) {
                canvas_count_at_close += canvases.get<engine::ui::UiCanvas>(entity).window == id ? 1 : 0;
            }
        }
        if (worlds != nullptr) {
            still_bound_at_close = worlds->world_for(id) != nullptr;
        }
        FakeWindowControl::close_window(id);
    }

    engine::ecs::World* world = nullptr;
    engine::Worlds* worlds = nullptr;
    int close_calls = 0;
    int canvas_count_at_close = -1;
    bool still_bound_at_close = true;
};

struct Fixture {
    Fixture() : worlds(fatal) {
        world = &worlds.add();
        windows.world = world;
        windows.worlds = &worlds;
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    std::expected<UiWindow, engine::UiWindowError> open(engine::WindowDesc desc = {},
            engine::ui::UiCanvas canvas = {}) {
        return UiWindow::open(windows, worlds, *world, desc, std::move(canvas));
    }

    void request_close(engine::WindowId window) {
        engine::ecs::EventWriter<engine::ui::WindowCloseRequestedEvent>{*world}.send(
                engine::ui::WindowCloseRequestedEvent{.window = window});
    }

    void frame() { world->run(engine::ecs::Schedule::Frame); }

    fakes::QuietFatal fatal;
    engine::Worlds worlds;
    WatchedWindows windows;
    engine::ecs::World* world = nullptr;
};

// Runs the destructor of the window inside `opened`, as leaving a scope would.
void destroy_window(std::expected<UiWindow, engine::UiWindowError>& opened) {
    UiWindow gone(std::move(*opened));
}

int canvases_on(engine::ecs::World& world, engine::WindowId window) {
    int count = 0;
    auto canvases = world.view<engine::ui::UiCanvas>();
    for (const engine::ecs::Entity entity : canvases) {
        count += canvases.get<engine::ui::UiCanvas>(entity).window == window ? 1 : 0;
    }
    return count;
}

// A 300 x 200 title-bar document: a drag-region-wide bar, a label, and a close button at x 270..294, y 4..26.
void give_title_bar(Fixture& fx, UiWindow& window, std::string_view css) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack class="bar">
<Label class="title" text="Settings"/><Button class="close" content="x"/></Stack></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    ASSERT_TRUE(sheet.has_value());
    engine::ui::UiCanvas& canvas = fx.world->get<engine::ui::UiCanvas>(window.canvas());
    canvas.rect = {0.0f, 0.0f, 300.0f, 200.0f};
    canvas.fit = engine::ui::UiFit::Fixed;
    if (fx.world->try_get<engine::ui::UiInstance>(window.canvas()) != nullptr) {
        fx.world->remove<engine::ui::UiInstance>(window.canvas());
    }
    fx.world->emplace<engine::ui::UiInstance>(
            window.canvas(), engine::ui::UiInstance{.document = std::move(*parsed), .stylesheet = std::move(*sheet)});
}

void lay_out(Fixture& fx, UiWindow& window) {
    auto& instance = fx.world->get<engine::ui::UiInstance>(window.canvas());
    engine::ui::apply_layout_style(instance.document.root, &*instance.stylesheet, 300.0f, 200.0f);
    engine::ui::layout(instance.document, engine::render::Rect{0.0f, 0.0f, 300.0f, 200.0f});
}

constexpr std::string_view kBarCss = R"(
.bar { width: 300; height: 30; }
.title { position: absolute; left: 8; top: 6; width: 100; height: 18; }
.close { position: absolute; left: 270; top: 4; width: 24; height: 22; }
)";

}

TEST(UiWindow, OpenBindsTheWindowEnablesUiAndSpawnsTheCanvasOnIt) {
    Fixture fx;
    auto window = fx.open(engine::WindowDesc{.title = "Settings"});
    ASSERT_TRUE(window.has_value());

    const engine::WindowId id = window->id();
    EXPECT_NE(id, engine::kPrimaryWindow);
    EXPECT_TRUE(window->is_open());
    EXPECT_EQ(fx.worlds.world_for(id), fx.world);
    EXPECT_EQ(fx.windows.titles.at(id), "Settings");
    EXPECT_TRUE(fx.worlds.draws_ui(*fx.world));
    ASSERT_TRUE(fx.world->valid(window->canvas()));
    EXPECT_EQ(fx.world->get<engine::ui::UiCanvas>(window->canvas()).window, id);
}

TEST(UiWindow, TheCanvasWindowTheCallerWroteIsReplacedByTheNewWindow) {
    Fixture fx;
    engine::ui::UiCanvas canvas;
    canvas.window = engine::WindowId{77};
    auto window = fx.open({}, canvas);
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(fx.world->get<engine::ui::UiCanvas>(window->canvas()).window, window->id());
}

TEST(UiWindow, ARefusedWindowIsAnErrorAndLeavesNothingBehind) {
    Fixture fx;
    fx.windows.refuse_open = true;
    const auto window = fx.open();

    ASSERT_FALSE(window.has_value());
    EXPECT_EQ(window.error(), engine::UiWindowError::WindowRefused);
    EXPECT_EQ(engine::to_string(window.error()), "WindowRefused");
    EXPECT_EQ(canvases_on(*fx.world, engine::WindowId{1}), 0);
    EXPECT_TRUE(fx.world->ctx<engine::BoundWindows>().ids.empty());
}

TEST(UiWindow, AnOwnerThatIsNotOpenIsRefused) {
    Fixture fx;
    const auto window = fx.open(engine::WindowDesc{.owner = engine::WindowId{42}});
    ASSERT_FALSE(window.has_value());
    EXPECT_EQ(window.error(), engine::UiWindowError::WindowRefused);
}

TEST(UiWindow, TeardownDestroysCanvasesThenUnbindsThenClosesTheWindow) {
    Fixture fx;
    engine::WindowId id{};
    {
        auto window = fx.open();
        ASSERT_TRUE(window.has_value());
        id = window->id();
        // The game spawned a second canvas on the same window, and one on another window.
        engine::ui::UiCanvas extra;
        extra.window = id;
        fx.world->emplace<engine::ui::UiCanvas>(fx.world->create(), extra);
        engine::ui::UiCanvas other;
        other.window = engine::kPrimaryWindow;
        fx.world->emplace<engine::ui::UiCanvas>(fx.world->create(), other);
        ASSERT_EQ(canvases_on(*fx.world, id), 2);
    }

    EXPECT_EQ(fx.windows.close_calls, 1);
    EXPECT_EQ(fx.windows.canvas_count_at_close, 0);
    EXPECT_FALSE(fx.windows.still_bound_at_close);
    EXPECT_FALSE(fx.windows.is_open(id));
    EXPECT_EQ(fx.worlds.world_for(id), nullptr);
    EXPECT_EQ(canvases_on(*fx.world, id), 0);
    // The canvas of another window is not this window's to destroy.
    EXPECT_EQ(canvases_on(*fx.world, engine::kPrimaryWindow), 1);
    EXPECT_TRUE(fx.world->ctx<engine::BoundWindows>().ids.empty());
}

TEST(UiWindow, ClosingTwiceClosesOnce) {
    Fixture fx;
    auto window = fx.open();
    ASSERT_TRUE(window.has_value());
    const engine::WindowId id = window->id();

    window->close();
    window->close();
    EXPECT_FALSE(window->is_open());
    EXPECT_EQ(fx.windows.close_calls, 1);

    destroy_window(window);
    EXPECT_EQ(fx.windows.close_calls, 1);
    EXPECT_FALSE(fx.windows.is_open(id));
}

TEST(UiWindow, MovingTransfersTheWindowAndTheSourceDoesNothing) {
    Fixture fx;
    auto first = fx.open();
    ASSERT_TRUE(first.has_value());
    const engine::WindowId id = first->id();
    const engine::ecs::Entity canvas = first->canvas();

    UiWindow moved(std::move(*first));
    EXPECT_FALSE(first->is_open());
    EXPECT_EQ(first->canvas(), engine::ecs::Entity{});
    first->close();  // a moved-from window has nothing to close
    EXPECT_EQ(fx.windows.close_calls, 0);

    EXPECT_TRUE(moved.is_open());
    EXPECT_EQ(moved.id(), id);
    EXPECT_EQ(moved.canvas(), canvas);
    EXPECT_TRUE(fx.world->valid(canvas));
}

TEST(UiWindow, MoveAssignmentClosesTheWindowItReplaces) {
    Fixture fx;
    auto first = fx.open();
    auto second = fx.open();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    const engine::WindowId first_id = first->id();
    const engine::WindowId second_id = second->id();

    *first = std::move(*second);

    EXPECT_FALSE(fx.windows.is_open(first_id));
    EXPECT_EQ(fx.worlds.world_for(first_id), nullptr);
    EXPECT_EQ(first->id(), second_id);
    EXPECT_TRUE(first->is_open());
    EXPECT_EQ(fx.windows.close_calls, 1);
}

TEST(UiWindow, TheOwnerWindowClosingClosesTheOwnedUiWindowWithoutASecondClose) {
    Fixture fx;
    const engine::WindowId parent = *fx.windows.open_window(engine::WindowDesc{.title = "Parent"});
    auto child = fx.open(engine::WindowDesc{.title = "Child", .owner = parent});
    ASSERT_TRUE(child.has_value());
    const engine::WindowId child_id = child->id();
    fx.windows.close_calls = 0;

    fx.windows.close_window(parent);  // the platform closes the owned window first
    EXPECT_FALSE(child->is_open());
    const int closes_by_platform = fx.windows.close_calls;  // the parent and the child

    child->close();
    EXPECT_EQ(fx.windows.close_calls, closes_by_platform);
    EXPECT_EQ(fx.worlds.world_for(child_id), nullptr);
    EXPECT_EQ(canvases_on(*fx.world, child_id), 0);

    destroy_window(child);
    EXPECT_EQ(fx.windows.close_calls, closes_by_platform);
}

TEST(UiWindow, ClosingAWindowWhoseWorldWasDestroyedOnlyClosesTheWindow) {
    Fixture fx;
    auto window = fx.open();
    ASSERT_TRUE(window.has_value());
    const engine::WindowId id = window->id();

    fx.windows.world = nullptr;
    fx.worlds.destroy(*fx.world);
    fx.world = nullptr;
    EXPECT_EQ(fx.worlds.world_for(id), nullptr);

    destroy_window(window);
    EXPECT_FALSE(fx.windows.is_open(id));
    EXPECT_EQ(fx.windows.close_calls, 1);
}

TEST(UiWindow, EachWindowGetsOnlyItsOwnCloseRequest) {
    Fixture fx;
    auto settings = fx.open();
    auto about = fx.open();
    ASSERT_TRUE(settings.has_value());
    ASSERT_TRUE(about.has_value());

    fx.request_close(about->id());
    fx.frame();

    EXPECT_FALSE(settings->take_close_requested());
    EXPECT_TRUE(about->take_close_requested());
    EXPECT_FALSE(about->take_close_requested());  // taken once

    fx.request_close(settings->id());
    fx.request_close(settings->id());
    fx.request_close(about->id());
    fx.frame();
    EXPECT_TRUE(settings->take_close_requested());
    EXPECT_TRUE(about->take_close_requested());
    EXPECT_TRUE(settings->is_open());  // the engine never closes it on its own
}

TEST(UiWindow, ACloseRequestForAnotherWindowIsNobodysBusiness) {
    Fixture fx;
    auto window = fx.open();
    ASSERT_TRUE(window.has_value());
    fx.request_close(engine::kPrimaryWindow);
    fx.frame();
    EXPECT_FALSE(window->take_close_requested());
}

TEST(UiWindow, TheDragRegionExcludesTheInteractiveChildrenOfTheCanvas) {
    Fixture fx;
    auto window = fx.open();
    ASSERT_TRUE(window.has_value());
    give_title_bar(fx, *window, kBarCss);
    lay_out(fx, *window);

    window->set_drag_region(engine::render::Rect{0.0f, 0.0f, 300.0f, 30.0f});
    EXPECT_EQ(fx.windows.drag_regions.at(window->id()), (engine::render::Rect{0.0f, 0.0f, 300.0f, 30.0f}));
    fx.frame();

    // The button, and not the label (it is not interactive).
    ASSERT_EQ(fx.windows.drag_exclusions[window->id()].size(), 1u);
    const engine::render::Rect hole = fx.windows.drag_exclusions[window->id()][0];
    EXPECT_FLOAT_EQ(hole.x, 270.0f);
    EXPECT_FLOAT_EQ(hole.y, 4.0f);
    EXPECT_FLOAT_EQ(hole.w, 24.0f);
    EXPECT_FLOAT_EQ(hole.h, 22.0f);
}

TEST(UiWindow, TheExclusionsFollowTheLayoutAndAreSentOnlyWhenTheyChange) {
    Fixture fx;
    auto window = fx.open();
    ASSERT_TRUE(window.has_value());
    give_title_bar(fx, *window, kBarCss);
    lay_out(fx, *window);
    window->set_drag_region(engine::render::Rect{0.0f, 0.0f, 300.0f, 30.0f});

    fx.frame();
    fx.frame();
    fx.frame();
    EXPECT_EQ(fx.windows.drag_exclusion_calls, 1);

    // The button moves left: the next frame's layout says so.
    give_title_bar(fx, *window, std::string(kBarCss) + ".close { left: 200; }");
    lay_out(fx, *window);
    fx.frame();
    EXPECT_EQ(fx.windows.drag_exclusion_calls, 2);
    EXPECT_FLOAT_EQ(fx.windows.drag_exclusions[window->id()][0].x, 200.0f);
}

TEST(UiWindow, ExclusionsAreInWindowPixelsWhenTheCanvasIsOffset) {
    Fixture fx;
    auto window = fx.open();
    ASSERT_TRUE(window.has_value());
    give_title_bar(fx, *window, kBarCss);
    lay_out(fx, *window);
    // A Fixed canvas is laid out into its own rect, so the element rects are already window pixels.
    fx.world->get<engine::ui::UiCanvas>(window->canvas()).rect = {20.0f, 10.0f, 300.0f, 200.0f};
    engine::ui::layout(fx.world->get<engine::ui::UiInstance>(window->canvas()).document,
            engine::render::Rect{20.0f, 10.0f, 300.0f, 200.0f});

    window->set_drag_region(engine::render::Rect{0.0f, 0.0f, 340.0f, 60.0f});
    fx.frame();

    ASSERT_EQ(fx.windows.drag_exclusions[window->id()].size(), 1u);
    const engine::render::Rect hole = fx.windows.drag_exclusions[window->id()][0];
    EXPECT_FLOAT_EQ(hole.x, 20.0f + 270.0f);
    EXPECT_FLOAT_EQ(hole.y, 10.0f + 4.0f);
    EXPECT_FLOAT_EQ(hole.w, 24.0f);
    EXPECT_FLOAT_EQ(hole.h, 22.0f);
}

TEST(UiWindow, ClearingTheDragRegionClearsTheExclusions) {
    Fixture fx;
    auto window = fx.open();
    ASSERT_TRUE(window.has_value());
    give_title_bar(fx, *window, kBarCss);
    lay_out(fx, *window);
    window->set_drag_region(engine::render::Rect{0.0f, 0.0f, 300.0f, 30.0f});
    fx.frame();
    ASSERT_FALSE(fx.windows.drag_exclusions[window->id()].empty());

    window->set_drag_region(std::nullopt);
    EXPECT_FALSE(fx.windows.drag_regions.at(window->id()).has_value());
    EXPECT_TRUE(fx.windows.drag_exclusions[window->id()].empty());
}

TEST(UiWindow, ANotYetLaidOutCanvasHasNoExclusionsAndNoCrash) {
    Fixture fx;
    auto window = fx.open();
    ASSERT_TRUE(window.has_value());
    window->set_drag_region(engine::render::Rect{0.0f, 0.0f, 300.0f, 30.0f});
    fx.frame();
    EXPECT_TRUE(fx.windows.drag_exclusions[window->id()].empty());
}

TEST(UiWindow, CenteredPositionIsTheMiddleOfTheArea) {
    EXPECT_EQ(engine::centered_position({0.0f, 0.0f, 1920.0f, 1040.0f}, {400, 300}), (glm::ivec2{760, 370}));
    // A second display to the right and below the origin.
    EXPECT_EQ(engine::centered_position({1920.0f, 100.0f, 1280.0f, 720.0f}, {200, 100}), (glm::ivec2{2460, 410}));
    // An odd difference rounds down, also below zero.
    EXPECT_EQ(engine::centered_position({0.0f, 0.0f, 101.0f, 101.0f}, {50, 50}), (glm::ivec2{25, 25}));
    EXPECT_EQ(engine::centered_position({0.0f, 0.0f, 100.0f, 100.0f}, {151, 101}), (glm::ivec2{-26, -1}));
}

TEST(UiWindow, CenterOnWorkAreaMovesTheWindowToTheMiddleOfItsDisplay) {
    Fixture fx;
    fx.windows.displays = {engine::render::Rect{0.0f, 0.0f, 1920.0f, 1040.0f}};
    auto window = fx.open(engine::WindowDesc{.size = {400, 300}});
    ASSERT_TRUE(window.has_value());

    window->center_on_work_area();
    EXPECT_EQ(fx.windows.positions.at(window->id()), (glm::ivec2{760, 370}));
}

TEST(UiWindow, CenterOnWorkAreaDoesNothingWithoutADisplay) {
    Fixture fx;
    auto window = fx.open(engine::WindowDesc{.size = {400, 300}, .position = glm::ivec2{5, 6}});
    ASSERT_TRUE(window.has_value());

    window->center_on_work_area();
    EXPECT_EQ(fx.windows.positions.at(window->id()), (glm::ivec2{5, 6}));
}

TEST(UiWindow, AClosedWindowIgnoresDragRegionAndCentering) {
    Fixture fx;
    fx.windows.displays = {engine::render::Rect{0.0f, 0.0f, 1920.0f, 1040.0f}};
    auto window = fx.open(engine::WindowDesc{.size = {400, 300}});
    ASSERT_TRUE(window.has_value());
    const engine::WindowId id = window->id();
    window->close();

    window->set_drag_region(engine::render::Rect{0.0f, 0.0f, 10.0f, 10.0f});
    window->center_on_work_area();
    EXPECT_EQ(fx.windows.drag_regions.count(id), 0u);
}
