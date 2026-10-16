#include <gtest/gtest.h>

#include <engine/core/input_system.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/pointer_target.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/sprite.h>
#include <engine/ui/command.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include <glm/gtc/constants.hpp>

#include <memory>
#include <vector>

namespace {

using engine::MouseButton;
using engine::MouseEvent;
using engine::PointerTarget;
using engine::PointerTargetEvent;

constexpr float kPixelsPerUnit = 30.0f;  // 600 px for ortho_size 10 (20 units)

// A world with the default camera at the origin, and an 800x600 primary window: world (x, y) is at screen
// (400 + 30 x, 300 - 30 y).
struct Scene {
    engine::ecs::World world;
    engine::ecs::Entity camera;
    engine::ecs::EventCursor<PointerTargetEvent> cursor;

    Scene() {
        camera = world.create();
        world.emplace<engine::Transform>(camera, engine::Transform{});
        world.emplace<engine::Camera>(camera, engine::Camera{});
        world.ctx<engine::ActiveCamera>().entity = camera;
        engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    }

    static glm::vec2 screen(float x, float y) { return {400.0f + kPixelsPerUnit * x, 300.0f - kPixelsPerUnit * y}; }

    engine::ecs::Entity box(glm::vec3 position, glm::vec2 size, int order = 0) {
        const engine::ecs::Entity entity = world.create();
        world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
        world.emplace<PointerTarget>(entity, PointerTarget{.size = size, .order_in_layer = order});
        return entity;
    }

    void move_to(engine::WindowId window, glm::vec2 at) {
        engine::ui::pointer_for(world, window).position = at;
        engine::ecs::EventWriter<MouseEvent>{world}.send(MouseEvent{
                .window = window, .kind = MouseEvent::Kind::Move, .position = at});
    }

    void press(engine::WindowId window, glm::vec2 at, MouseButton button = MouseButton::Left) {
        engine::ui::pointer_for(world, window).position = at;
        engine::ecs::EventWriter<MouseEvent>{world}.send(MouseEvent{
                .window = window, .kind = MouseEvent::Kind::Down, .position = at, .button = button});
    }

    void release(engine::WindowId window, glm::vec2 at, MouseButton button = MouseButton::Left) {
        engine::ui::pointer_for(world, window).position = at;
        engine::ecs::EventWriter<MouseEvent>{world}.send(MouseEvent{
                .window = window, .kind = MouseEvent::Kind::Up, .position = at, .button = button});
    }

    // One frame of the pointer part of Input, then the events it sent.
    std::vector<PointerTargetEvent> frame() {
        engine::run_pointer_targets(world);
        std::vector<PointerTargetEvent> sent;
        for (const PointerTargetEvent& event : engine::ecs::EventReader<PointerTargetEvent>{world, cursor}) {
            sent.push_back(event);
        }
        world.flush_events();
        engine::ui::reset_pointer_frame(engine::ui::presentation_of(world));
        return sent;
    }
};

class ClickViewModel final : public engine::ui::ViewModel {
public:
    int clicks = 0;
    engine::ui::RelayCommand click;

    ClickViewModel() {
        command(engine::ui::intern("click"), click);
        click = [this] { ++clicks; };
    }
};

std::size_t count(const std::vector<PointerTargetEvent>& events, PointerTargetEvent::Kind kind) {
    std::size_t n = 0;
    for (const PointerTargetEvent& event : events) {
        n += event.kind == kind ? 1 : 0;
    }
    return n;
}

}

TEST(PointerTarget, HitInsideAndOutsideARect) {
    Scene scene;
    const auto box = scene.box({2.0f, 1.0f, 0.0f}, {4.0f, 2.0f});  // x 0..4, y 0..2

    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(2.0f, 1.0f)), box);
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(3.9f, 1.9f)), box);
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(0.1f, 0.1f)), box);
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(4.2f, 1.0f)));
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(2.0f, 2.2f)));
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(-0.2f, 1.0f)));
}

TEST(PointerTarget, PivotMovesTheRectAroundTheEntity) {
    Scene scene;
    const auto entity = scene.world.create();
    scene.world.emplace<engine::Transform>(entity, engine::Transform{.position = {1.0f, 1.0f, 0.0f}});
    scene.world.emplace<PointerTarget>(entity, PointerTarget{.size = {2.0f, 2.0f}, .pivot = {0.0f, 0.0f}});

    // Pivot {0, 0}: the entity is the bottom-left corner, so the rect is x 1..3, y 1..3.
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(2.5f, 2.5f)), entity);
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(0.5f, 0.5f)));
}

TEST(PointerTarget, ScaledAndRotatedTransformsMoveTheRect) {
    Scene scene;
    const auto entity = scene.world.create();
    scene.world.emplace<engine::Transform>(entity,
            engine::Transform{.position = {0.0f, 0.0f, 0.0f},
                    .rotation = engine::rotation_z(glm::half_pi<float>()),
                    .scale = {1.0f, 4.0f, 1.0f}});
    scene.world.emplace<PointerTarget>(entity, PointerTarget{.size = {1.0f, 1.0f}});

    // Local rect 1 x 1 scaled to 1 x 4 and turned a quarter: it is now 4 wide and 1 tall around the origin.
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(1.8f, 0.0f)), entity);
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(-1.8f, 0.4f)), entity);
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(0.0f, 1.8f)));
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(2.4f, 0.0f)));
}

TEST(PointerTarget, ChildFollowsItsParent) {
    Scene scene;
    const auto parent = scene.world.create();
    scene.world.emplace<engine::Transform>(parent, engine::Transform{.position = {5.0f, 0.0f, 0.0f}});
    const auto child = scene.box({1.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    ASSERT_TRUE(engine::ecs::set_parent(scene.world, child, parent));
    engine::ecs::update_world_transforms(scene.world);

    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(6.0f, 0.0f)), child);
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(1.0f, 0.0f)));
}

TEST(PointerTarget, TopmostBySortOrderWinsOverlappingTargets) {
    Scene scene;
    const auto low = scene.box({0.0f, 0.0f, 0.0f}, {4.0f, 4.0f}, 1);
    const auto high = scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f}, 5);
    const auto later_same_order = scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f}, 5);

    // Highest order, and between equals the later entity, as the draw order.
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f)),
            later_same_order);
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(1.5f, 1.5f)), low);
    scene.world.get<PointerTarget>(later_same_order).enabled = false;
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f)), high);
}

TEST(PointerTarget, SpriteLayerDecidesTheOrderWhenThereIsASprite) {
    Scene scene;
    const auto behind = scene.box({0.0f, 0.0f, 0.0f}, {4.0f, 4.0f}, 100);
    scene.world.emplace<engine::render::Sprite>(behind, engine::render::Sprite{.layer = 0});
    const auto front = scene.box({0.0f, 0.0f, 0.0f}, {4.0f, 4.0f}, 0);
    scene.world.emplace<engine::render::Sprite>(front, engine::render::Sprite{.layer = 2});

    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f)), front);
}

TEST(PointerTarget, SizeIsDerivedFromTheSprite) {
    Scene scene;
    const auto entity = scene.world.create();
    scene.world.emplace<engine::Transform>(entity, engine::Transform{.position = {2.0f, 0.0f, 0.0f}});
    scene.world.emplace<engine::render::Sprite>(entity,
            engine::render::Sprite{.pixel_size = {200.0f, 100.0f}, .pixels_per_unit = 100.0f});  // 2 x 1 units
    scene.world.emplace<PointerTarget>(entity);

    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(2.9f, 0.4f)), entity);
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(3.2f, 0.0f)));
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(2.0f, 0.7f)));
}

TEST(PointerTarget, NoSizeAndNoSpriteSizeIsNeverHit) {
    Scene scene;
    const auto entity = scene.world.create();
    scene.world.emplace<engine::Transform>(entity, engine::Transform{});
    scene.world.emplace<PointerTarget>(entity);
    scene.world.emplace<engine::render::Sprite>(entity, engine::render::Sprite{});

    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f)));
}

TEST(PointerTarget, CameraPanAndZoomAreAppliedToTheCursor) {
    Scene scene;
    const auto box = scene.box({10.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    // Off screen (the window shows x -13.3..13.3, but not screen x 700 at the origin camera: 400 + 30 * 10 = 700).
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, {700.0f, 300.0f}), box);

    // Pan the camera right by 10: the box is now in the middle of the window.
    scene.world.get<engine::Transform>(scene.camera).position.x = 10.0f;
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, {400.0f, 300.0f}), box);
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, {700.0f, 300.0f}));

    // Zoom in (ortho_size 5): one unit is 60 px, the box spans 120 px around the centre.
    scene.world.get<engine::Camera>(scene.camera).ortho_size = 5.0f;
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, {450.0f, 330.0f}), box);
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, {470.0f, 300.0f}));
}

TEST(PointerTarget, EachWindowUsesItsOwnSizeAndItsOwnTargets) {
    Scene scene;
    const engine::WindowId second{1};
    engine::ui::presentation_of(scene.world).sizes.sizes[second] = engine::ui::WindowSize{400, 300};
    const auto everywhere = scene.box({2.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    const auto only_second = scene.box({-2.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    scene.world.get<PointerTarget>(only_second).window = second;

    // The second window is half the size at the same ortho size: 15 px per unit, centre at (200, 150).
    EXPECT_EQ(engine::pointer_target_at(scene.world, second, {200.0f + 30.0f, 150.0f}), everywhere);
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(2.0f, 0.0f)), everywhere);
    // The window-bound target answers only in its window.
    EXPECT_EQ(engine::pointer_target_at(scene.world, second, {200.0f - 30.0f, 150.0f}), only_second);
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(-2.0f, 0.0f)));
}

TEST(PointerTarget, NoCameraOrNoWindowSizeMeansNoHit) {
    Scene scene;
    scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::WindowId{9}, {10.0f, 10.0f}));

    scene.world.ctx<engine::ActiveCamera>().entity = {};
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f)));
}

TEST(PointerTarget, HoverEnterAndExitAreReportedOnceEach) {
    Scene scene;
    const auto box = scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});

    scene.move_to(engine::kPrimaryWindow, Scene::screen(5.0f, 5.0f));
    EXPECT_TRUE(scene.frame().empty());

    scene.move_to(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    auto events = scene.frame();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, PointerTargetEvent::Kind::Enter);
    EXPECT_EQ(events[0].entity, box);
    EXPECT_NEAR(events[0].world_position.x, 0.0f, 1e-3f);
    EXPECT_TRUE(scene.world.get<PointerTarget>(box).hovered);

    // Still over it, moving inside: nothing new, hover stays.
    scene.move_to(engine::kPrimaryWindow, Scene::screen(0.5f, 0.5f));
    EXPECT_TRUE(scene.frame().empty());
    EXPECT_TRUE(scene.frame().empty());
    EXPECT_TRUE(scene.world.get<PointerTarget>(box).hovered);

    scene.move_to(engine::kPrimaryWindow, Scene::screen(5.0f, 5.0f));
    events = scene.frame();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, PointerTargetEvent::Kind::Exit);
    EXPECT_FALSE(scene.world.get<PointerTarget>(box).hovered);
}

TEST(PointerTarget, MovingBetweenTwoTargetsExitsThenEnters) {
    Scene scene;
    const auto left = scene.box({-3.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    const auto right = scene.box({3.0f, 0.0f, 0.0f}, {2.0f, 2.0f});

    scene.move_to(engine::kPrimaryWindow, Scene::screen(-3.0f, 0.0f));
    ASSERT_EQ(scene.frame().size(), 1u);
    scene.move_to(engine::kPrimaryWindow, Scene::screen(3.0f, 0.0f));
    const auto events = scene.frame();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].kind, PointerTargetEvent::Kind::Exit);
    EXPECT_EQ(events[0].entity, left);
    EXPECT_EQ(events[1].kind, PointerTargetEvent::Kind::Enter);
    EXPECT_EQ(events[1].entity, right);
}

TEST(PointerTarget, AMovingTargetEntersWhenItArrivesUnderAStillCursor) {
    Scene scene;
    const auto box = scene.box({6.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    scene.move_to(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    EXPECT_TRUE(scene.frame().empty());

    scene.world.get<engine::Transform>(box).position.x = 0.0f;
    const auto events = scene.frame();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, PointerTargetEvent::Kind::Enter);
}

TEST(PointerTarget, PressThenReleaseOnTheSameTargetIsAClick) {
    Scene scene;
    const auto box = scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    const glm::vec2 at = Scene::screen(0.0f, 0.0f);

    scene.press(engine::kPrimaryWindow, at);
    auto events = scene.frame();
    EXPECT_EQ(count(events, PointerTargetEvent::Kind::Press), 1u);
    EXPECT_EQ(count(events, PointerTargetEvent::Kind::Click), 0u);
    EXPECT_TRUE(scene.world.get<PointerTarget>(box).pressed);

    scene.release(engine::kPrimaryWindow, at);
    events = scene.frame();
    ASSERT_EQ(count(events, PointerTargetEvent::Kind::Click), 1u);
    EXPECT_FALSE(scene.world.get<PointerTarget>(box).pressed);
    for (const PointerTargetEvent& event : events) {
        if (event.kind == PointerTargetEvent::Kind::Click) {
            EXPECT_EQ(event.entity, box);
            EXPECT_EQ(event.button, MouseButton::Left);
        }
    }
}

TEST(PointerTarget, ReleaseElsewhereOrWithAnotherButtonIsNotAClick) {
    Scene scene;
    scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    scene.box({5.0f, 0.0f, 0.0f}, {2.0f, 2.0f});

    scene.press(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    (void)scene.frame();
    scene.release(engine::kPrimaryWindow, Scene::screen(5.0f, 0.0f));
    EXPECT_EQ(count(scene.frame(), PointerTargetEvent::Kind::Click), 0u);

    scene.press(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f), MouseButton::Left);
    (void)scene.frame();
    scene.release(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f), MouseButton::Right);
    EXPECT_EQ(count(scene.frame(), PointerTargetEvent::Kind::Click), 0u);
    // The left press is still pending; the left release completes it.
    scene.release(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f), MouseButton::Left);
    EXPECT_EQ(count(scene.frame(), PointerTargetEvent::Kind::Click), 1u);
}

TEST(PointerTarget, PressOnEmptySpaceReportsNothing) {
    Scene scene;
    scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    scene.press(engine::kPrimaryWindow, Scene::screen(8.0f, 8.0f));
    EXPECT_TRUE(scene.frame().empty());
    scene.release(engine::kPrimaryWindow, Scene::screen(8.0f, 8.0f));
    EXPECT_TRUE(scene.frame().empty());
}

TEST(PointerTarget, ADisabledTargetExitsAndCannotBePressed) {
    Scene scene;
    const auto box = scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    scene.move_to(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    ASSERT_EQ(scene.frame().size(), 1u);

    scene.world.get<PointerTarget>(box).enabled = false;
    const auto events = scene.frame();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, PointerTargetEvent::Kind::Exit);
    EXPECT_FALSE(scene.world.get<PointerTarget>(box).hovered);

    scene.press(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    EXPECT_TRUE(scene.frame().empty());
}

TEST(PointerTarget, ADestroyedHoveredTargetIsForgottenWithoutAnEvent) {
    Scene scene;
    const auto box = scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    scene.move_to(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    ASSERT_EQ(scene.frame().size(), 1u);

    scene.world.destroy(box);
    EXPECT_TRUE(scene.frame().empty());
    EXPECT_TRUE(scene.frame().empty());
}

TEST(PointerTarget, ATargetUnderTheCursorIsCountedForClickThrough) {
    Scene scene;
    scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    engine::ui::Presentation& presentation = engine::ui::presentation_of(scene.world);

    scene.move_to(engine::kPrimaryWindow, Scene::screen(8.0f, 8.0f));
    engine::run_pointer_targets(scene.world);
    EXPECT_FALSE(presentation.mouse.consumed_for(engine::kPrimaryWindow));

    scene.move_to(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    engine::run_pointer_targets(scene.world);
    EXPECT_TRUE(presentation.mouse.consumed_for(engine::kPrimaryWindow));
}

TEST(PointerTarget, ClickThroughStaysCountedWhileTheCursorRestsOnATarget) {
    Scene scene;
    scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    engine::ui::Presentation& presentation = engine::ui::presentation_of(scene.world);
    scene.move_to(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    (void)scene.frame();

    // The next frame has no mouse event at all, and the set was cleared at its start.
    engine::run_pointer_targets(scene.world);
    EXPECT_TRUE(presentation.mouse.consumed_for(engine::kPrimaryWindow));
}

TEST(PointerTarget, UiOverATargetTakesThePointerAndNothingIsReported) {
    Scene scene;
    scene.box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f});
    engine::ui::Presentation& presentation = engine::ui::presentation_of(scene.world);

    scene.move_to(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    ASSERT_EQ(scene.frame().size(), 1u);  // Enter, no UI yet

    // The UI took this frame's pointer (it inserts the window like a hit does): the target is exited, not pressed.
    scene.move_to(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    presentation.mouse.consumed_windows.insert(engine::kPrimaryWindow);
    auto events = scene.frame();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, PointerTargetEvent::Kind::Exit);

    scene.press(engine::kPrimaryWindow, Scene::screen(0.0f, 0.0f));
    presentation.mouse.consumed_windows.insert(engine::kPrimaryWindow);
    EXPECT_EQ(count(scene.frame(), PointerTargetEvent::Kind::Press), 0u);
}

TEST(PointerTarget, TheRegisteredSystemsLetTheUiTakeThePointerBeforeATargetBelowIt) {
    Scene scene;
    const auto box = scene.box({0.0f, 0.0f, 0.0f}, {8.0f, 8.0f});  // screen x 280..520, y 180..420

    auto vm = std::make_shared<ClickViewModel>();
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Button command="{binding click}" content="Go"/></Canvas>)", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());
    const engine::ecs::Entity canvas_entity = scene.world.create();
    engine::ui::UiCanvas canvas;
    canvas.rect = {380.0f, 280.0f, 100.0f, 100.0f};
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;
    scene.world.emplace<engine::ui::UiCanvas>(canvas_entity, canvas);
    scene.world.emplace<engine::ui::UiInstance>(canvas_entity, engine::ui::UiInstance{*parsed});
    engine::register_engine_systems(scene.world, engine::EngineSystemDeps{});

    const auto run_frame = [&] {
        engine::ui::reset_pointer_frame(engine::ui::presentation_of(scene.world));
        scene.world.run(engine::ecs::Schedule::Frame);
        std::vector<PointerTargetEvent> sent;
        for (const PointerTargetEvent& event : engine::ecs::EventReader<PointerTargetEvent>{scene.world, scene.cursor}) {
            sent.push_back(event);
        }
        scene.world.flush_events();
        return sent;
    };

    // On the button: the UI runs its command and the target below sees nothing, though it is under the cursor.
    scene.press(engine::kPrimaryWindow, {384.0f, 284.0f});
    EXPECT_TRUE(run_frame().empty());
    EXPECT_EQ(vm->clicks, 1);
    EXPECT_FALSE(scene.world.get<PointerTarget>(box).pressed);
    EXPECT_FALSE(scene.world.get<PointerTarget>(box).hovered);
    scene.release(engine::kPrimaryWindow, {384.0f, 284.0f});
    EXPECT_TRUE(run_frame().empty());

    // Beside the button, on the target: the target gets it and the UI does not.
    scene.press(engine::kPrimaryWindow, {300.0f, 200.0f});
    auto events = run_frame();
    EXPECT_EQ(count(events, PointerTargetEvent::Kind::Press), 1u);
    EXPECT_EQ(vm->clicks, 1);
    EXPECT_TRUE(scene.world.get<PointerTarget>(box).pressed);
    EXPECT_TRUE(engine::ui::presentation_of(scene.world).mouse.consumed_for(engine::kPrimaryWindow));
    scene.release(engine::kPrimaryWindow, {300.0f, 200.0f});
    EXPECT_EQ(count(run_frame(), PointerTargetEvent::Kind::Click), 1u);
}

TEST(PointerTarget, PerspectiveCameraHitsTargetsOnTheirOwnPlane) {
    Scene scene;
    engine::Camera camera;
    camera.projection = engine::Projection::Perspective;
    camera.near_clip = 0.1f;
    camera.far_clip = 100.0f;
    scene.world.get<engine::Camera>(scene.camera) = camera;
    scene.world.get<engine::Transform>(scene.camera).position = {0.0f, 0.0f, 10.0f};

    // A target 2 units right of the view axis on z = 0, and one at z = 5 that the same world x puts farther out on
    // screen: each is hit where it projects, not at the other's depth.
    const auto flat = scene.box({2.0f, 0.0f, 0.0f}, {1.0f, 1.0f});
    const auto raised = scene.box({2.0f, 3.0f, 5.0f}, {1.0f, 1.0f});
    const auto& cam = scene.world.get<engine::Camera>(scene.camera);
    const glm::mat4 camera_world = engine::model_matrix(scene.world.get<engine::Transform>(scene.camera));
    const engine::ui::WindowSize size{800, 600};

    const glm::vec2 flat_at = engine::world_to_screen({2.0f, 0.0f, 0.0f}, cam, camera_world, size);
    const glm::vec2 raised_at = engine::world_to_screen({2.0f, 3.0f, 5.0f}, cam, camera_world, size);
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, flat_at), flat);
    EXPECT_EQ(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, raised_at), raised);
    EXPECT_FALSE(engine::pointer_target_at(scene.world, engine::kPrimaryWindow, {400.0f, 300.0f}));
}
