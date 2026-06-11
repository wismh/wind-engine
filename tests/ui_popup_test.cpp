#include <gtest/gtest.h>

#include "ui/bind_scan.h"
#include "ui/painter.h"
#include "ui/popup.h"

#include <engine/core/input_system.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using engine::render::Rect;
using engine::ui::BoxInsets;
using engine::ui::Element;
using engine::ui::ElementKind;
using engine::ui::PopupPlacement;

class RecordingPainter final : public engine::ui::IUiPainter {
public:
    std::vector<Rect> scissors;
    std::vector<glm::vec2> view_pans;
    std::vector<std::string> texts;

    void save() override {}
    void restore() override {}
    void scissor(const Rect& rect) override { scissors.push_back(rect); }
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2 pan, float) override { view_pans.push_back(pan); }
    void set_opacity(float) override {}
    void fill_rounded_rect(const Rect&, float, glm::vec4) override {}
    void fill_rounded_rect_gradient(const Rect&, float, const engine::ui::Gradient&) override {}
    void stroke_rounded_rect(const Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
    void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
    void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override {}
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view text, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {
        texts.emplace_back(text);
    }
    void image(engine::AssetId, const Rect&) override {}
    void image_repeat(engine::AssetId, const Rect&) override {}
    void image_nine_slice(engine::AssetId, const Rect&, const BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
};

class MenuViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<bool> menuOpen;
    engine::ui::Bindable<bool> subOpen;
    engine::ui::Bindable<float> scroll;
    engine::ui::Bindable<std::string> name;
    engine::ui::RelayCommand toggle;
    engine::ui::RelayCommand toggleSub;
    engine::ui::RelayCommand rename;
    engine::ui::RelayCommand remove;
    engine::ui::RelayCommand link;
    engine::ui::RelayCommand under;
    int renamed = 0;
    int removed = 0;
    int linked = 0;
    int unders = 0;

    MenuViewModel() {
        property(engine::ui::intern("menuOpen"), menuOpen);
        property(engine::ui::intern("subOpen"), subOpen);
        property(engine::ui::intern("scroll"), scroll);
        property(engine::ui::intern("name"), name);
        command(engine::ui::intern("toggle"), toggle);
        command(engine::ui::intern("toggleSub"), toggleSub);
        command(engine::ui::intern("rename"), rename);
        command(engine::ui::intern("remove"), remove);
        command(engine::ui::intern("link"), link);
        command(engine::ui::intern("under"), under);
        toggle = [this] { menuOpen.set(!menuOpen.get()); };
        toggleSub = [this] { subOpen.set(!subOpen.get()); };
        rename = [this] { ++renamed; };
        remove = [this] { ++removed; };
        link = [this] { ++linked; };
        under = [this] { ++unders; };
    }
};

class RowViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<bool> open;
    engine::ui::RelayCommand toggle;

    RowViewModel() {
        property(engine::ui::intern("open"), open);
        command(engine::ui::intern("toggle"), toggle);
        toggle = [this] { open.set(!open.get()); };
    }
};

class ListViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<RowViewModel>> rows;

    ListViewModel() { property(engine::ui::intern("rows"), rows); }
};

// `more` opens `menu` under itself; `under` sits exactly where the menu shows.
constexpr std::string_view kMenuXml = R"(
<Canvas>
  <Stack>
    <Button id="more" command="{binding toggle}">
      <Popup id="menu" open="{binding menuOpen}">
        <Button id="rename" content="Rename" command="{binding rename}"/>
        <Button id="remove" content="Remove" command="{binding remove}"/>
      </Popup>
    </Button>
    <Button id="under" command="{binding under}"/>
  </Stack>
</Canvas>
)";

// Button 100x40, so `more` is 0..40 and `under` 40..80. The menu is 128x68 at (0, 40):
// rename 44..74, remove 74..104.
constexpr std::string_view kMenuCss = R"(
Button { width: 100px; height: 40px; }
Popup { padding: 4px; }
Popup Button { width: 120px; height: 30px; }
)";

engine::ecs::Entity add_canvas(engine::ecs::World& world, std::string_view xml, std::string_view css,
        std::shared_ptr<engine::ui::ViewModel> vm, Rect rect, int order = 0) {
    auto document = engine::ui::parse_xml(xml, nullptr, vm.get());
    EXPECT_TRUE(document.has_value());
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    engine::ui::UiCanvas canvas;
    canvas.rect = rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.order = order;
    canvas.data_context = std::move(vm);
    return engine::ui::spawn_canvas(world, std::move(canvas), std::move(*document), std::move(*sheet));
}

Element* find_id(Element& element, std::string_view id) {
    if (element.id == id) {
        return &element;
    }
    for (Element& child : element.children) {
        if (Element* found = find_id(child, id)) {
            return found;
        }
    }
    for (Element& child : element.generated_items) {
        if (Element* found = find_id(child, id)) {
            return found;
        }
    }
    return nullptr;
}

Element& root_of(engine::ecs::World& world, engine::ecs::Entity canvas) {
    return world.get<engine::ui::UiInstance>(canvas).document.root;
}

void set_window(engine::ecs::World& world, int width, int height) {
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{width, height};
}

// What run_bind does at the start of a frame: input reads `open` as the last bind left it.
void bind(engine::ecs::World& world, engine::ecs::Entity canvas) {
    engine::ui::UiCanvas& ui = world.get<engine::ui::UiCanvas>(canvas);
    (void) engine::ui::apply_bindings(world.get<engine::ui::UiInstance>(canvas).document, *ui.data_context);
}

void click(engine::ecs::World& world, float x, float y) {
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, x, y);
}

} // namespace

TEST(UiPopup, XmlParsesOpenAndPlacement) {
    MenuViewModel vm;
    auto doc = engine::ui::parse_xml(R"(
        <Canvas>
          <Button>
            <Popup id="bound" open="{binding menuOpen}" placement="right-end"/>
          </Button>
          <Button>
            <Popup id="literal" open="true"/>
          </Button>
          <Button open="true" placement="nowhere"/>
        </Canvas>
    )", nullptr, &vm);
    ASSERT_TRUE(doc.has_value());
    const Element* bound = find_id(doc->root, "bound");
    ASSERT_NE(bound, nullptr);
    EXPECT_EQ(bound->kind, ElementKind::Popup);
    EXPECT_EQ(bound->open_binding, engine::ui::intern("menuOpen"));
    EXPECT_EQ(bound->placement, PopupPlacement::RightEnd);
    EXPECT_FALSE(bound->open);
    const Element* literal = find_id(doc->root, "literal");
    ASSERT_NE(literal, nullptr);
    EXPECT_TRUE(literal->open);
    EXPECT_EQ(literal->placement, PopupPlacement::BottomStart);
    // `open` and `placement` mean nothing on another element, like any unknown attribute.
    EXPECT_FALSE(doc->root.children[2].open);
}

TEST(UiPopup, UnknownPlacementIsInvalidMarkup) {
    auto doc = engine::ui::parse_xml(R"(<Canvas><Button><Popup placement="under"/></Button></Canvas>)");
    ASSERT_FALSE(doc.has_value());
    EXPECT_EQ(doc.error(), engine::ui::UiError::InvalidMarkup);
}

TEST(UiPopup, UnregisteredOpenBindingIsMissingBinding) {
    MenuViewModel vm;
    auto doc = engine::ui::parse_xml(R"(<Canvas><Button><Popup open="{binding nope}"/></Button></Canvas>)",
            nullptr, &vm);
    ASSERT_FALSE(doc.has_value());
    EXPECT_EQ(doc.error(), engine::ui::UiError::MissingBinding);
}

TEST(UiPopup, CodegenScanRegistersOpen) {
    auto binder = engine::ui::scan_bind_tree(kMenuXml);
    ASSERT_TRUE(binder.has_value());
    const auto& members = binder->members;
    const auto open = std::find_if(members.begin(), members.end(),
            [](const engine::ui::BindMember& member) { return member.path == "menuOpen"; });
    ASSERT_NE(open, members.end());
    EXPECT_FALSE(open->is_command);
}

TEST(UiPopup, BuilderMakesPopup) {
    using namespace engine::ui;
    auto doc = make_document(canvas().add(button().add(
            popup().open_bind(intern("menuOpen")).placement(PopupPlacement::TopEnd).add(label().text("x")))));
    ASSERT_TRUE(doc.has_value());
    const Element* popup_element = find_by_kind(doc->root, ElementKind::Popup);
    ASSERT_NE(popup_element, nullptr);
    EXPECT_EQ(popup_element->open_binding, intern("menuOpen"));
    EXPECT_EQ(popup_element->placement, PopupPlacement::TopEnd);
    ASSERT_EQ(popup_element->children.size(), 1u);

    auto literal = make_document(canvas().add(button().add(popup().open(true))));
    ASSERT_TRUE(literal.has_value());
    EXPECT_TRUE(find_by_kind(literal->root, ElementKind::Popup)->open);
}

TEST(UiPopup, TakesNoSpaceInTheFlow) {
    auto doc = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="column">
            <Stack id="row" direction="horizontal">
              <Button id="more"><Popup id="menu" open="true"><Button id="item"/></Popup></Button>
              <Button id="next"/>
            </Stack>
            <Button id="below"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(doc.has_value());
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css("Button { width: 50px; height: 20px; } #item { width: 300px; height: 90px; }",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    engine::ui::apply_layout_style(doc->root, &*sheet, 400.0f, 300.0f);
    engine::ui::layout(*doc, Rect{0.0f, 0.0f, 400.0f, 300.0f});

    const Element& row = *find_id(doc->root, "row");
    EXPECT_FLOAT_EQ(row.layout_rect.w, 100.0f);
    EXPECT_FLOAT_EQ(row.layout_rect.h, 20.0f);
    EXPECT_FLOAT_EQ(find_id(doc->root, "next")->layout_rect.x, 50.0f);
    EXPECT_FLOAT_EQ(find_id(doc->root, "below")->layout_rect.y, 20.0f);
    // Laid out at its anchor's top-left, sized by its content.
    const Element& menu = *find_id(doc->root, "menu");
    EXPECT_EQ(menu.layout_rect, (Rect{0.0f, 0.0f, 300.0f, 90.0f}));
    EXPECT_EQ(find_id(doc->root, "item")->layout_rect, (Rect{0.0f, 0.0f, 300.0f, 90.0f}));
}

TEST(UiPopup, PlaceRectSidesAndAlignment) {
    const Rect bounds{0.0f, 0.0f, 400.0f, 300.0f};
    const Rect anchor{100.0f, 100.0f, 40.0f, 20.0f};
    const glm::vec2 size{60.0f, 30.0f};
    const BoxInsets none{};
    EXPECT_EQ(engine::ui::place_popup_rect(anchor, size, none, PopupPlacement::BottomStart, bounds),
            (Rect{100.0f, 120.0f, 60.0f, 30.0f}));
    EXPECT_EQ(engine::ui::place_popup_rect(anchor, size, none, PopupPlacement::BottomEnd, bounds),
            (Rect{80.0f, 120.0f, 60.0f, 30.0f}));
    EXPECT_EQ(engine::ui::place_popup_rect(anchor, size, none, PopupPlacement::TopStart, bounds),
            (Rect{100.0f, 70.0f, 60.0f, 30.0f}));
    EXPECT_EQ(engine::ui::place_popup_rect(anchor, size, none, PopupPlacement::RightStart, bounds),
            (Rect{140.0f, 100.0f, 60.0f, 30.0f}));
    EXPECT_EQ(engine::ui::place_popup_rect(anchor, size, none, PopupPlacement::LeftEnd, bounds),
            (Rect{40.0f, 90.0f, 60.0f, 30.0f}));
    // Only the margin facing the anchor is the gap.
    const BoxInsets margin{4.0f, 8.0f, 6.0f, 2.0f};
    EXPECT_EQ(engine::ui::place_popup_rect(anchor, size, margin, PopupPlacement::BottomStart, bounds),
            (Rect{100.0f, 124.0f, 60.0f, 30.0f}));
    EXPECT_EQ(engine::ui::place_popup_rect(anchor, size, margin, PopupPlacement::TopStart, bounds),
            (Rect{100.0f, 64.0f, 60.0f, 30.0f}));
    EXPECT_EQ(engine::ui::place_popup_rect(anchor, size, margin, PopupPlacement::RightStart, bounds),
            (Rect{142.0f, 100.0f, 60.0f, 30.0f}));
    EXPECT_EQ(engine::ui::place_popup_rect(anchor, size, margin, PopupPlacement::LeftStart, bounds),
            (Rect{32.0f, 100.0f, 60.0f, 30.0f}));
}

TEST(UiPopup, PlaceRectFlipsWhenTheOtherSideIsRoomier) {
    const Rect bounds{0.0f, 0.0f, 400.0f, 300.0f};
    const glm::vec2 size{60.0f, 50.0f};
    // 20 below, 260 above: flips up.
    EXPECT_EQ(engine::ui::place_popup_rect(Rect{100.0f, 260.0f, 40.0f, 20.0f}, size, {}, PopupPlacement::BottomStart,
                      bounds),
            (Rect{100.0f, 210.0f, 60.0f, 50.0f}));
    // 10 above, 270 below: a top placement flips down.
    EXPECT_EQ(engine::ui::place_popup_rect(Rect{100.0f, 10.0f, 40.0f, 20.0f}, size, {}, PopupPlacement::TopStart,
                      bounds),
            (Rect{100.0f, 30.0f, 60.0f, 50.0f}));
    // Right side too narrow: flips left.
    EXPECT_EQ(engine::ui::place_popup_rect(Rect{350.0f, 100.0f, 40.0f, 20.0f}, size, {}, PopupPlacement::RightStart,
                      bounds),
            (Rect{290.0f, 100.0f, 60.0f, 50.0f}));
    // Neither side fits and below is roomier: stays below, pushed up inside the window.
    EXPECT_EQ(engine::ui::place_popup_rect(Rect{100.0f, 20.0f, 40.0f, 20.0f}, glm::vec2{60.0f, 290.0f}, {},
                      PopupPlacement::BottomStart, bounds),
            (Rect{100.0f, 10.0f, 60.0f, 290.0f}));
}

TEST(UiPopup, PlaceRectIsPushedInsideTheWindow) {
    const Rect bounds{0.0f, 0.0f, 400.0f, 300.0f};
    EXPECT_EQ(engine::ui::place_popup_rect(Rect{380.0f, 100.0f, 20.0f, 20.0f}, glm::vec2{60.0f, 30.0f}, {},
                      PopupPlacement::BottomStart, bounds),
            (Rect{340.0f, 120.0f, 60.0f, 30.0f}));
    EXPECT_EQ(engine::ui::place_popup_rect(Rect{0.0f, 100.0f, 20.0f, 20.0f}, glm::vec2{60.0f, 30.0f}, {},
                      PopupPlacement::BottomEnd, bounds),
            (Rect{0.0f, 120.0f, 60.0f, 30.0f}));
    // Wider than the window: starts at its left edge.
    EXPECT_EQ(engine::ui::place_popup_rect(Rect{100.0f, 100.0f, 20.0f, 20.0f}, glm::vec2{500.0f, 30.0f}, {},
                      PopupPlacement::BottomStart, bounds),
            (Rect{0.0f, 120.0f, 500.0f, 30.0f}));
}

TEST(UiPopup, PopupBoundsAreTheWindowInLayoutUnits) {
    const engine::ui::UiCanvasSpace scaled{Rect{0.0f, 0.0f, 200.0f, 100.0f}, glm::vec2{50.0f, 0.0f}, 2.0f, true};
    EXPECT_EQ(engine::ui::popup_bounds(scaled, engine::ui::WindowSize{500, 200}),
            (Rect{-25.0f, 0.0f, 250.0f, 100.0f}));
    // No window size yet: the canvas.
    const engine::ui::UiCanvasSpace fixed{Rect{10.0f, 20.0f, 30.0f, 40.0f}, glm::vec2{0.0f, 0.0f}, 1.0f, false};
    EXPECT_EQ(engine::ui::popup_bounds(fixed, engine::ui::WindowSize{}), fixed.layout_rect);
}

TEST(UiPopup, ClickReachesThePopupOverWhatIsUnderIt) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    add_canvas(world, kMenuXml, kMenuCss, vm, Rect{0.0f, 0.0f, 400.0f, 300.0f});

    click(world, 50.0f, 20.0f);
    ASSERT_TRUE(vm->menuOpen.get());

    // `under` is at 40..80, but the menu is above it.
    click(world, 50.0f, 60.0f);
    EXPECT_EQ(vm->renamed, 1);
    EXPECT_EQ(vm->unders, 0);
    EXPECT_TRUE(vm->menuOpen.get());

    // Outside the anchor's box and the Stack's: still the menu.
    click(world, 110.0f, 90.0f);
    EXPECT_EQ(vm->removed, 1);
    EXPECT_TRUE(engine::ui::presentation_of(world).mouse.consumed_for());
}

TEST(UiPopup, ClickOutsideClosesWithoutReachingAnything) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    bind(world, add_canvas(world, kMenuXml, kMenuCss, vm, Rect{0.0f, 0.0f, 400.0f, 300.0f}));

    click(world, 300.0f, 200.0f);
    EXPECT_FALSE(vm->menuOpen.get());
    EXPECT_TRUE(engine::ui::presentation_of(world).mouse.consumed_for());

    vm->menuOpen.set(true);
    engine::ui::reset_pointer_frame(engine::ui::presentation_of(world));
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 300.0f, 10.0f, engine::kPrimaryWindow, false);
    EXPECT_FALSE(vm->menuOpen.get()) << "any button dismisses";

    click(world, 50.0f, 60.0f);
    EXPECT_EQ(vm->unders, 1);
    EXPECT_EQ(vm->renamed, 0);
}

TEST(UiPopup, ClickOnTheAnchorIsLeftToItsCommand) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    bind(world, add_canvas(world, kMenuXml, kMenuCss, vm, Rect{0.0f, 0.0f, 400.0f, 300.0f}));

    // The toggle closes it; light dismiss does not close it first and let the toggle reopen it.
    click(world, 50.0f, 20.0f);
    EXPECT_FALSE(vm->menuOpen.get());
    click(world, 50.0f, 20.0f);
    EXPECT_TRUE(vm->menuOpen.get());
}

TEST(UiPopup, FollowsItsAnchorThroughScroll) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    const engine::ecs::Entity canvas = add_canvas(world, R"(
        <Canvas>
          <ScrollView scroll-y="{binding scroll}">
            <Button id="first" command="{binding under}"/>
            <Button id="more" command="{binding toggle}">
              <Popup id="menu" open="{binding menuOpen}">
                <Button id="rename" command="{binding rename}"/>
              </Popup>
            </Button>
            <Button id="last" command="{binding under}"/>
          </ScrollView>
        </Canvas>
    )", "ScrollView { width: 100px; height: 100px; } Button { width: 100px; height: 40px; } "
        "Popup Button { width: 120px; height: 30px; }",
            vm, Rect{0.0f, 0.0f, 400.0f, 300.0f});

    bind(world, canvas);
    // `more` is 40..80, so the menu is 80..110 and sticks out of the 100px ScrollView.
    click(world, 50.0f, 105.0f);
    EXPECT_EQ(vm->renamed, 1);
    EXPECT_EQ(find_id(root_of(world, canvas), "menu")->popup_offset, (glm::vec2{0.0f, 40.0f}));

    vm->scroll.set(20.0f);
    click(world, 50.0f, 65.0f);
    EXPECT_EQ(vm->renamed, 2);
    EXPECT_EQ(find_id(root_of(world, canvas), "menu")->popup_offset, (glm::vec2{0.0f, 20.0f}));
}

TEST(UiPopup, EscapesAFixedCanvasUpToTheWindow) {
    engine::ecs::World world;
    set_window(world, 400, 300);
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    bind(world, add_canvas(world, kMenuXml, kMenuCss, vm, Rect{0.0f, 0.0f, 200.0f, 80.0f}));

    // Below the canvas rect: remove is 74..104.
    click(world, 50.0f, 100.0f);
    EXPECT_EQ(vm->removed, 1);
}

TEST(UiPopup, FlipsAboveItsAnchorAtTheBottomOfTheWindow) {
    engine::ecs::World world;
    set_window(world, 400, 300);
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    const engine::ecs::Entity canvas = add_canvas(world, kMenuXml,
            std::string(kMenuCss) + " Stack { padding: 240px 0px 0px 0px; }", vm, Rect{0.0f, 0.0f, 400.0f, 300.0f});
    bind(world, canvas);

    // `more` is 240..280: 20 below, 240 above. The menu is 68 tall: 172..240.
    click(world, 50.0f, 180.0f);
    EXPECT_EQ(vm->renamed, 1);
    EXPECT_EQ(find_id(root_of(world, canvas), "menu")->popup_offset, (glm::vec2{0.0f, -68.0f}));
}

TEST(UiPopup, LowerCanvasPopupIsAboveAHigherCanvas) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    bind(world, add_canvas(world, kMenuXml, kMenuCss, vm, Rect{0.0f, 0.0f, 400.0f, 300.0f}, 0));
    auto panel_vm = std::make_shared<MenuViewModel>();
    add_canvas(world, R"(<Canvas><Stack><Button command="{binding under}"/></Stack></Canvas>)",
            "Button { width: 200px; height: 200px; }", panel_vm, Rect{0.0f, 50.0f, 200.0f, 200.0f}, 1);

    click(world, 50.0f, 60.0f);
    EXPECT_EQ(vm->renamed, 1);
    EXPECT_EQ(panel_vm->unders, 0);

    // Outside the menu the higher canvas is hit again, after the click that closes the menu.
    click(world, 50.0f, 150.0f);
    EXPECT_FALSE(vm->menuOpen.get());
    EXPECT_EQ(panel_vm->unders, 0);
    click(world, 50.0f, 150.0f);
    EXPECT_EQ(panel_vm->unders, 1);
}

constexpr std::string_view kNestedXml = R"(
<Canvas>
  <Stack>
    <Button id="more" command="{binding toggle}">
      <Popup id="menu" open="{binding menuOpen}">
        <Button id="share" command="{binding toggleSub}">
          <Popup id="sub" open="{binding subOpen}" placement="right-start">
            <Button id="link" command="{binding link}"/>
          </Popup>
        </Button>
        <Button id="remove" command="{binding remove}"/>
      </Popup>
    </Button>
  </Stack>
</Canvas>
)";

// menu at (0, 40) 120x60: share 40..70, remove 70..100. sub right of share: (120, 40).
constexpr std::string_view kNestedCss = R"(
Button { width: 100px; height: 40px; }
Popup Button { width: 120px; height: 30px; }
)";

TEST(UiPopup, NestedPopupIsAboveItsParent) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    vm->subOpen.set(true);
    bind(world, add_canvas(world, kNestedXml, kNestedCss, vm, Rect{0.0f, 0.0f, 400.0f, 300.0f}));

    click(world, 130.0f, 50.0f);
    EXPECT_EQ(vm->linked, 1);
    EXPECT_TRUE(vm->menuOpen.get());
    EXPECT_TRUE(vm->subOpen.get());

    // A click in the parent menu closes only the submenu, and still runs.
    click(world, 50.0f, 80.0f);
    EXPECT_EQ(vm->removed, 1);
    EXPECT_TRUE(vm->menuOpen.get());
    EXPECT_FALSE(vm->subOpen.get());
}

TEST(UiPopup, SubmenuAnchorTogglesIt) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    bind(world, add_canvas(world, kNestedXml, kNestedCss, vm, Rect{0.0f, 0.0f, 400.0f, 300.0f}));

    click(world, 50.0f, 50.0f);
    EXPECT_TRUE(vm->subOpen.get());
    click(world, 50.0f, 50.0f);
    EXPECT_FALSE(vm->subOpen.get());
    EXPECT_TRUE(vm->menuOpen.get());
}

TEST(UiPopup, EscapeClosesTheTopmostPopupFirst) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    vm->subOpen.set(true);
    bind(world, add_canvas(world, kNestedXml, kNestedCss, vm, Rect{0.0f, 0.0f, 400.0f, 300.0f}));

    engine::ui::handle_key(world, engine::KeyCode::Escape, true);
    EXPECT_TRUE(vm->menuOpen.get());
    EXPECT_FALSE(vm->subOpen.get());
    engine::ui::handle_key(world, engine::KeyCode::Escape, true);
    EXPECT_FALSE(vm->menuOpen.get());
}

TEST(UiPopup, EscapeAndOutsideClickDropFocusInsideThePopup) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    bind(world, add_canvas(world, R"(
        <Canvas>
          <Stack>
            <Button id="more" command="{binding toggle}">
              <Popup open="{binding menuOpen}"><TextInput id="field" text="{binding name}"/></Popup>
            </Button>
          </Stack>
        </Canvas>
    )", "Button { width: 100px; height: 40px; } TextInput { width: 120px; height: 30px; }", vm,
            Rect{0.0f, 0.0f, 400.0f, 300.0f}));

    click(world, 50.0f, 50.0f);
    const Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);
    EXPECT_EQ(focused->id, "field");

    engine::ui::handle_key(world, engine::KeyCode::Escape, true);
    EXPECT_FALSE(vm->menuOpen.get());
    EXPECT_EQ(engine::ui::focused_element(world), nullptr);
}

TEST(UiPopup, WheelOutsideClosesAndScrollsWheelInsideStays) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    const engine::ecs::Entity canvas = add_canvas(world, R"(
        <Canvas>
          <ScrollView scroll-y="{binding scroll}">
            <Button id="more" command="{binding toggle}">
              <Popup open="{binding menuOpen}"><Button command="{binding rename}"/></Popup>
            </Button>
            <Button/>
            <Button/>
            <Button/>
          </ScrollView>
        </Canvas>
    )", "ScrollView { width: 100px; height: 100px; } Button { width: 100px; height: 40px; } "
        "Popup Button { width: 120px; height: 30px; }",
            vm, Rect{0.0f, 0.0f, 400.0f, 300.0f});
    bind(world, canvas);

    // Over the popup (40..70): nothing under it scrolls.
    engine::ui::begin_frame(world);
    engine::ui::handle_wheel(world, 50.0f, 50.0f, -1.0f);
    EXPECT_TRUE(vm->menuOpen.get());
    EXPECT_FLOAT_EQ(vm->scroll.get(), 0.0f);
    EXPECT_TRUE(engine::ui::presentation_of(world).mouse.consumed_for());

    // Over the list, below the popup: closes it and scrolls.
    engine::ui::begin_frame(world);
    engine::ui::handle_wheel(world, 50.0f, 90.0f, -1.0f);
    EXPECT_FALSE(vm->menuOpen.get());
    EXPECT_GT(vm->scroll.get(), 0.0f);
}

TEST(UiPopup, RowPopupWritesItsOwnRowViewModel) {
    engine::ecs::World world;
    auto vm = std::make_shared<ListViewModel>();
    auto first = std::make_shared<RowViewModel>();
    auto second = std::make_shared<RowViewModel>();
    vm->rows.set({first, second});
    add_canvas(world, R"(
        <Canvas>
          <ItemsControl items_source="{binding rows}">
            <ItemTemplate>
              <Stack direction="horizontal">
                <Button command="{binding toggle}">
                  <Popup open="{binding open}"><Button/></Popup>
                </Button>
              </Stack>
            </ItemTemplate>
          </ItemsControl>
        </Canvas>
    )", "Button { width: 100px; height: 40px; } Popup Button { width: 120px; height: 30px; }", vm,
            Rect{0.0f, 0.0f, 400.0f, 300.0f});

    click(world, 50.0f, 60.0f);
    EXPECT_FALSE(first->open.get());
    EXPECT_TRUE(second->open.get());

    // Its popup is over the end of the list; a click past it closes it through the row's view-model.
    click(world, 300.0f, 200.0f);
    EXPECT_FALSE(second->open.get());
}

TEST(UiPopup, InspectorSeesThePopupWhereItIsShown) {
    auto doc = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack>
            <Button id="more"><Popup open="true"><Button id="rename"/></Popup></Button>
            <Button id="under"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(doc.has_value());
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(kMenuCss, warnings);
    engine::ui::apply_layout_style(doc->root, &*sheet, 400.0f, 300.0f);
    engine::ui::layout(*doc, Rect{0.0f, 0.0f, 400.0f, 300.0f});
    engine::ui::place_popups(doc->root, Rect{0.0f, 0.0f, 400.0f, 300.0f});

    const engine::ui::VisualHit hit = engine::ui::hit_test_visual(doc->root, 50.0f, 50.0f);
    ASSERT_NE(hit.element, nullptr);
    EXPECT_EQ(hit.element->id, "rename");
    EXPECT_EQ(hit.boxes.border, (Rect{4.0f, 44.0f, 120.0f, 30.0f}));
    EXPECT_EQ(engine::ui::layout_boxes(doc->root, *find_id(doc->root, "rename")).border,
            (Rect{4.0f, 44.0f, 120.0f, 30.0f}));
}

TEST(UiPopup, BasePassSkipsThePopupAndTheLayerDrawsItAboveTheWindowClip) {
    auto doc = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack>
            <Button id="more" content="More"><Popup open="true"><Button content="Rename"/></Popup></Button>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(doc.has_value());
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(kMenuCss, warnings);
    ASSERT_TRUE(sheet.has_value());

    RecordingPainter painter;
    engine::ui::UiPaintInput input{.canvas_rect = Rect{0.0f, 0.0f, 200.0f, 80.0f}, .window_width = 400.0f,
            .window_height = 300.0f};
    input.popup_bounds = Rect{0.0f, 0.0f, 400.0f, 300.0f};
    engine::ui::paint_document(*doc, &*sheet, painter, input);
    EXPECT_NE(std::find(painter.texts.begin(), painter.texts.end(), "More"), painter.texts.end());
    EXPECT_EQ(std::find(painter.texts.begin(), painter.texts.end(), "Rename"), painter.texts.end());

    painter = RecordingPainter{};
    input.popup_layer = true;
    engine::ui::paint_document(*doc, &*sheet, painter, input);
    EXPECT_EQ(painter.texts, (std::vector<std::string>{"Rename"}));
    ASSERT_FALSE(painter.scissors.empty());
    EXPECT_EQ(painter.scissors.front(), (Rect{0.0f, 0.0f, 400.0f, 300.0f}));
    ASSERT_EQ(painter.view_pans.size(), 1u);
    EXPECT_EQ(painter.view_pans.front(), (glm::vec2{0.0f, 40.0f}));

    // Closed: the layer draws nothing.
    find_by_kind(doc->root, ElementKind::Popup)->open = false;
    painter = RecordingPainter{};
    engine::ui::paint_document(*doc, &*sheet, painter, input);
    EXPECT_TRUE(painter.texts.empty());
    EXPECT_TRUE(painter.scissors.empty());
}

TEST(UiPopup, RenderPushesPopupLayersAfterEveryBasePass) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    set_window(world, 400, 300);
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    const engine::ecs::Entity low = add_canvas(world, kMenuXml, kMenuCss, vm, Rect{0.0f, 0.0f, 400.0f, 300.0f}, 0);
    auto panel_vm = std::make_shared<MenuViewModel>();
    const engine::ecs::Entity high =
            add_canvas(world, R"(<Canvas><Stack><Button command="{binding under}"/></Stack></Canvas>)",
                    "Button { width: 200px; height: 200px; }", panel_vm, Rect{0.0f, 50.0f, 200.0f, 200.0f}, 1);
    // A previous frame bound, laid out, and placed the open menu.
    bind(world, low);
    engine::ui::update_pointer_hover(world, 50.0f, 60.0f);

    // The pointer rests on the menu, over the higher canvas.
    engine::ecs::EventWriter<engine::MouseEvent>{world}.send(engine::MouseEvent{
            .window = engine::kPrimaryWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {50.0f, 60.0f},
    });
    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 3u);
    const auto& base_low = std::get<engine::render::CmdDrawUI>(commands[0]);
    const auto& base_high = std::get<engine::render::CmdDrawUI>(commands[1]);
    const auto& layer = std::get<engine::render::CmdDrawUI>(commands[2]);
    EXPECT_EQ(base_low.document, &world.get<engine::ui::UiInstance>(low).document);
    EXPECT_FALSE(base_low.popup_layer);
    EXPECT_EQ(base_high.document, &world.get<engine::ui::UiInstance>(high).document);
    EXPECT_FALSE(base_high.popup_layer);
    EXPECT_EQ(layer.document, base_low.document);
    EXPECT_TRUE(layer.popup_layer);
    EXPECT_EQ(layer.popup_bounds, (Rect{0.0f, 0.0f, 400.0f, 300.0f}));
    // The owner of the popup under the pointer keeps it; the canvas it covers does not hover.
    EXPECT_EQ(base_low.pointer, (glm::vec2{50.0f, 60.0f}));
    EXPECT_LT(base_high.pointer.y, -1.0e8f);
}

TEST(UiPopup, RenderPushesNoLayerWhileClosed) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    auto vm = std::make_shared<MenuViewModel>();
    add_canvas(world, kMenuXml, kMenuCss, vm, Rect{0.0f, 0.0f, 400.0f, 300.0f});

    world.run(engine::ecs::Schedule::Frame);
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_FALSE(std::get<engine::render::CmdDrawUI>(commands[0]).popup_layer);

    vm->menuOpen.set(true);
    world.run(engine::ecs::Schedule::Frame);
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_TRUE(std::get<engine::render::CmdDrawUI>(commands[1]).popup_layer);
}

TEST(UiPopup, ScrollbarDragInsideAPopupMovesByThePointerDelta) {
    engine::ecs::World world;
    auto vm = std::make_shared<MenuViewModel>();
    vm->menuOpen.set(true);
    const engine::ecs::Entity canvas = add_canvas(world, R"(
        <Canvas>
          <Stack>
            <Button id="more" command="{binding toggle}">
              <Popup id="menu" open="{binding menuOpen}"><Button/><Button/><Button/></Popup>
            </Button>
          </Stack>
        </Canvas>
    )", "Button { width: 100px; height: 40px; } Popup Button { width: 120px; height: 30px; } "
        "Popup { height: 60px; overflow-y: auto; scrollbar-width: 10px; }",
            vm, Rect{0.0f, 0.0f, 400.0f, 300.0f});
    bind(world, canvas);

    // Shown at y 40..100; 90 of content in 60, so the thumb is 40 tall with 20 of travel over 30 of scroll.
    click(world, 115.0f, 50.0f);
    engine::ui::update_drag(world, 115.0f, 60.0f);
    EXPECT_FLOAT_EQ(find_id(root_of(world, canvas), "menu")->scroll_y, 15.0f);
    engine::ui::end_drag(world);
}
