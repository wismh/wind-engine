#include <gtest/gtest.h>

#include <engine/ecs/world.h>
#include <engine/ui/bindable.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "fixtures/bind_error_fakes.h"
#include "ui/painter.h"

namespace {

// One inventory slot: a position, a stacking order, and its own drag fraction.
class SlotViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<float> x;
    engine::ui::Bindable<float> y;
    engine::ui::Bindable<int> z;
    engine::ui::Bindable<float> fraction;

    SlotViewModel(float px, float py, int pz = 0) : x(px), y(py), z(pz) {
        property(engine::ui::intern("x"), x);
        property(engine::ui::intern("y"), y);
        property(engine::ui::intern("z"), z);
        property(engine::ui::intern("fraction"), fraction);
    }
};

class BoardViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<SlotViewModel>> slots;

    BoardViewModel() { property(engine::ui::intern("slots"), slots); }
};

constexpr std::string_view kBoardXml = R"(<Canvas>
  <Stack class="board">
    <ItemsControl class="slots" items_source="{binding slots}">
      <ItemTemplate>
        <Stack class="slot" var-x="{binding x}" var-y="{binding y}" var-z="{binding z}" drag="{binding fraction}"/>
      </ItemTemplate>
    </ItemsControl>
  </Stack>
</Canvas>)";

// The slots are placed by position inside the board, the nearest positioned ancestor. Their height is a CSS pixel
// value on the template root: the case that used to make the list virtualize as if the slots were stacked rows.
constexpr std::string_view kBoardCss = R"(
.board { position: relative; width: 300; height: 200; }
.slots { width: 300; height: 200; }
.slot { position: absolute; left: var(--x); top: var(--y); width: 40; height: 40; z-index: var(--z); }
)";

engine::ui::Stylesheet parse_sheet(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());
    return std::move(*sheet);
}

struct Board {
    engine::ecs::World world;
    std::shared_ptr<BoardViewModel> vm = std::make_shared<BoardViewModel>();
    engine::ecs::Entity entity;

    explicit Board(std::string_view xml = kBoardXml, std::string_view css = kBoardCss) {
        entity = world.create();
        engine::ui::UiCanvas canvas;
        canvas.rect = {0.0f, 0.0f, 400.0f, 400.0f};
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.data_context = vm;
        world.emplace<engine::ui::UiCanvas>(entity, canvas);
        auto parsed = engine::ui::parse_xml(xml, nullptr, vm.get());
        EXPECT_TRUE(parsed.has_value());
        world.emplace<engine::ui::UiInstance>(
                entity, engine::ui::UiInstance{.document = std::move(*parsed), .stylesheet = parse_sheet(css)});
    }

    engine::ui::UiDocument& document() { return world.get<engine::ui::UiInstance>(entity).document; }

    // What one rendered frame does for this canvas: bind, style, lay out.
    void frame() {
        auto& instance = world.get<engine::ui::UiInstance>(entity);
        ASSERT_TRUE(engine::ui::apply_bindings(instance.document, *vm).has_value());
        engine::ui::apply_layout_style(instance.document.root, &*instance.stylesheet, 400.0f, 400.0f);
        engine::ui::layout(instance.document, engine::render::Rect{0.0f, 0.0f, 400.0f, 400.0f});
    }

    const std::vector<engine::ui::Element>& items() {
        return document().root.children[0].children[0].generated_items;
    }

    std::shared_ptr<SlotViewModel> add(float x, float y, int z = 0) {
        auto slot = std::make_shared<SlotViewModel>(x, y, z);
        vm->slots.get().push_back(slot);
        return slot;
    }
};

}

TEST(UiAbsoluteItems, ItemsLandAtTheirBoundPositions) {
    Board board;
    board.add(10.0f, 20.0f);
    board.add(120.5f, 60.0f);
    board.add(250.0f, 150.0f);
    board.frame();

    ASSERT_EQ(board.items().size(), 3u);
    EXPECT_FLOAT_EQ(board.items()[0].layout_rect.x, 10.0f);
    EXPECT_FLOAT_EQ(board.items()[0].layout_rect.y, 20.0f);
    EXPECT_FLOAT_EQ(board.items()[0].layout_rect.w, 40.0f);
    EXPECT_FLOAT_EQ(board.items()[1].layout_rect.x, 120.5f);
    EXPECT_FLOAT_EQ(board.items()[1].layout_rect.y, 60.0f);
    EXPECT_FLOAT_EQ(board.items()[2].layout_rect.x, 250.0f);
    EXPECT_FLOAT_EQ(board.items()[2].layout_rect.y, 150.0f);
}

TEST(UiAbsoluteItems, MovingOneBindableMovesOnlyThatItem) {
    Board board;
    const auto first = board.add(10.0f, 10.0f);
    board.add(100.0f, 100.0f);
    board.frame();

    first->x.set(200.0f);
    first->y.set(5.0f);
    board.frame();

    EXPECT_FLOAT_EQ(board.items()[0].layout_rect.x, 200.0f);
    EXPECT_FLOAT_EQ(board.items()[0].layout_rect.y, 5.0f);
    EXPECT_FLOAT_EQ(board.items()[1].layout_rect.x, 100.0f);
    EXPECT_FLOAT_EQ(board.items()[1].layout_rect.y, 100.0f);
}

TEST(UiAbsoluteItems, AbsoluteRowsAreNotVirtualizedAwayInAScrollView) {
    Board board(R"(<Canvas><ScrollView class="scroll"><Stack class="board"><ItemsControl class="slots"
items_source="{binding slots}"><ItemTemplate><Stack class="slot" var-x="{binding x}" var-y="{binding y}"/></ItemTemplate>
</ItemsControl></Stack></ScrollView></Canvas>)",
            R"(.scroll { width: 300; height: 100; overflow-y: auto; }
.board { position: relative; width: 300; height: 600; }
.slots { width: 300; height: 600; }
.slot { position: absolute; left: var(--x); top: var(--y); width: 40; height: 40; })");
    for (int i = 0; i < 24; ++i) {
        board.add(static_cast<float>((i % 6) * 45), static_cast<float>((i / 6) * 150));
    }

    // Several frames: the window is decided from rows sampled on the previous frame.
    for (int i = 0; i < 4; ++i) {
        board.frame();
    }

    const auto& items = board.document().root.children[0].children[0].children[0].generated_items;
    EXPECT_EQ(items.size(), 24u);
    for (const engine::ui::Element& item : items) {
        EXPECT_NE(item.generated_owner, nullptr);
    }
}

TEST(UiAbsoluteItems, DraggingOneItemWritesOnlyItsOwnBindable) {
    Board board;
    const auto a = board.add(10.0f, 10.0f);
    const auto b = board.add(100.0f, 100.0f);
    const auto c = board.add(200.0f, 20.0f);
    engine::ui::begin_frame(board.world);

    // Inside b: x 100..140, so 130 is 0.75 along the default horizontal axis.
    engine::ui::handle_pointer(board.world, 130.0f, 110.0f);
    EXPECT_FLOAT_EQ(b->fraction.get(), 0.75f);
    EXPECT_FLOAT_EQ(a->fraction.get(), 0.0f);
    EXPECT_FLOAT_EQ(c->fraction.get(), 0.0f);

    engine::ui::update_drag(board.world, 5000.0f, 0.0f);
    EXPECT_FLOAT_EQ(b->fraction.get(), 1.0f);
    EXPECT_FLOAT_EQ(a->fraction.get(), 0.0f);
}

TEST(UiAbsoluteItems, TheHigherBoundZIndexIsTheItemThatTakesThePointer) {
    Board board;
    const auto back = board.add(10.0f, 10.0f, 1);
    const auto front = board.add(20.0f, 10.0f, 5);  // overlaps back, later in the list and a higher z
    engine::ui::begin_frame(board.world);

    engine::ui::handle_pointer(board.world, 30.0f, 20.0f);
    EXPECT_GT(front->fraction.get(), 0.0f);
    EXPECT_FLOAT_EQ(back->fraction.get(), 0.0f);
    engine::ui::end_drag(board.world);

    // Swap the stacking order through the bindables: the other item takes the pointer.
    back->z.set(9);
    engine::ui::handle_pointer(board.world, 30.0f, 20.0f);
    EXPECT_GT(back->fraction.get(), 0.0f);
}

TEST(UiAbsoluteItems, AddingAndRemovingItemsKeepsTheOthersState) {
    Board board;
    const auto a = board.add(10.0f, 10.0f);
    const auto b = board.add(100.0f, 100.0f);
    const auto c = board.add(200.0f, 20.0f);
    engine::ui::begin_frame(board.world);
    engine::ui::handle_pointer(board.world, 130.0f, 110.0f);
    engine::ui::end_drag(board.world);
    ASSERT_FLOAT_EQ(b->fraction.get(), 0.75f);
    board.frame();

    board.vm->slots.get().erase(board.vm->slots.get().begin());  // a leaves
    const auto d = board.add(50.0f, 150.0f);                     // d arrives
    board.frame();

    ASSERT_EQ(board.items().size(), 3u);
    EXPECT_FLOAT_EQ(board.items()[0].layout_rect.x, 100.0f);  // b
    EXPECT_FLOAT_EQ(board.items()[1].layout_rect.x, 200.0f);  // c
    EXPECT_FLOAT_EQ(board.items()[2].layout_rect.x, 50.0f);   // d
    EXPECT_FLOAT_EQ(board.items()[2].layout_rect.y, 150.0f);
    EXPECT_FLOAT_EQ(b->fraction.get(), 0.75f);
    EXPECT_FLOAT_EQ(c->fraction.get(), 0.0f);
    EXPECT_FLOAT_EQ(d->fraction.get(), 0.0f);
    EXPECT_FLOAT_EQ(a->fraction.get(), 0.0f);
}

TEST(UiAbsoluteItems, AMissingNameInTheTemplateIsNamedOnceForEachRow) {
    Board board(R"(<Canvas><Stack class="board"><ItemsControl class="slots" items_source="{binding slots}"><ItemTemplate>
<Stack class="slot" var-x="{binding nope}"/></ItemTemplate></ItemsControl></Stack></Canvas>)");
    board.add(1.0f, 1.0f);
    board.add(2.0f, 2.0f);
    fakes::RecordingBindErrors errors;
    auto& instance = board.world.get<engine::ui::UiInstance>(board.entity);

    const auto applied = engine::ui::apply_bindings(instance.document, *board.vm, &errors);

    ASSERT_FALSE(applied.has_value());
    EXPECT_EQ(applied.error(), engine::ui::UiError::MissingBinding);
    ASSERT_EQ(errors.messages.size(), 2u);
    EXPECT_EQ(errors.messages[0], "UI var-x binding \"nope\" is not registered on the data context");
    // The rows are still generated and the rest of the document still binds.
    EXPECT_EQ(board.items().size(), 2u);
}
