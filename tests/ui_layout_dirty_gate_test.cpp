#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using engine::ui::Element;
using engine::ui::ElementKind;
using engine::ui::layout_state_changed;

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return *sheet;
}

// Minimal no-op IUiPainter for the paint_document() smoke tests below — they only care about
// layout_rect outcomes, not what paint_document actually draws, so every call is a no-op and
// measure_text() returns the same cheap fallback layout()'s own no-painter path uses.
class FakePainterStub final : public engine::ui::IUiPainter {
public:
    // Extra bookkeeping for the Group C ("paint isn't frozen by the gate") tests below: the lowest
    // opacity seen across all set_opacity() calls this instance recorded (keyframe animation), the
    // last color a fill_rounded_rect() call carried (:hover background), and how many caret/line
    // draws happened (TextInput caret blink). None of this affects the earlier Крок 1-3 smoke tests
    // above, which never read these fields.
    float min_opacity = 1.0f;
    glm::vec4 last_fill_color{-1.0f, -1.0f, -1.0f, -1.0f};
    int draw_line_calls = 0;

    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
    void set_opacity(float opacity) override { min_opacity = std::min(min_opacity, opacity); }
    void fill_rounded_rect(const engine::render::Rect&, float, glm::vec4 color) override { last_fill_color = color; }
    void fill_rounded_rect_gradient(const engine::render::Rect&, float, const engine::ui::Gradient&) override {}
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override { ++draw_line_calls; }
    void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
    void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override {}
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {}
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {}

    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
};

// wind-129 layout dirty-gate: smoke tests for layout_state_changed() in isolation, without wiring
// it into prepare_top_canvas/paint_document (that's a later step). Just proves the function
// compiles, recurses into children, and its cached copies update on every call — not "stuck" on
// true once something has changed once.
TEST(UiLayoutDirtyGate, FirstCallAlwaysReportsChanged) {
    Element root;
    root.kind = ElementKind::Canvas;
    Element& label = root.children.emplace_back();
    label.kind = ElementKind::Label;
    label.text = "Hello";

    EXPECT_TRUE(layout_state_changed(root));
}

TEST(UiLayoutDirtyGate, SecondCallWithNoChangesReportsUnchanged) {
    Element root;
    root.kind = ElementKind::Canvas;
    Element& label = root.children.emplace_back();
    label.kind = ElementKind::Label;
    label.text = "Hello";

    ASSERT_TRUE(layout_state_changed(root));
    EXPECT_FALSE(layout_state_changed(root));
}

TEST(UiLayoutDirtyGate, TextChangeOnChildReportsChangedThenSettles) {
    Element root;
    root.kind = ElementKind::Canvas;
    Element& label = root.children.emplace_back();
    label.kind = ElementKind::Label;
    label.text = "Hello";

    ASSERT_TRUE(layout_state_changed(root));   // 1st call: never compared before.
    ASSERT_FALSE(layout_state_changed(root));  // 2nd call: nothing changed.

    label.text = "Hello, world!";
    EXPECT_TRUE(layout_state_changed(root));  // 3rd call: text differs from cached copy.

    // 4th call: the cached copy from the 3rd call must have been refreshed to the new text, not
    // stuck reporting "changed" forever.
    EXPECT_FALSE(layout_state_changed(root));
}

TEST(UiLayoutDirtyGate, CustomPropertyChangeReportsChangedThenSettles) {
    Element root;
    root.kind = ElementKind::Canvas;
    Element& child = root.children.emplace_back();
    child.kind = ElementKind::Stack;
    child.custom_properties["accent"] = "#ff0000";

    ASSERT_TRUE(layout_state_changed(root));
    ASSERT_FALSE(layout_state_changed(root));

    child.custom_properties["accent"] = "#00ff00";
    EXPECT_TRUE(layout_state_changed(root));
    EXPECT_FALSE(layout_state_changed(root));
}

TEST(UiLayoutDirtyGate, GeneratedOwnerSequenceChangeReportsChangedThenSettles) {
    Element root;
    root.kind = ElementKind::Canvas;
    Element& items = root.children.emplace_back();
    items.kind = ElementKind::ItemsControl;

    int owner_a = 0;
    int owner_b = 0;
    Element& row_a = items.generated_items.emplace_back();
    row_a.kind = ElementKind::Stack;
    row_a.generated_owner = &owner_a;

    ASSERT_TRUE(layout_state_changed(root));
    ASSERT_FALSE(layout_state_changed(root));

    Element& row_b = items.generated_items.emplace_back();
    row_b.kind = ElementKind::Stack;
    row_b.generated_owner = &owner_b;

    EXPECT_TRUE(layout_state_changed(root));
    EXPECT_FALSE(layout_state_changed(root));
}

// Same set of owners, different order: a reorder shifts every subsequent row's on-screen position,
// so it must be reported as layout-relevant even though no owner was added or removed — this is
// exactly what the vector (order-sensitive) comparison in layout_state_changed is for, as opposed
// to comparing an unordered set of owners.
TEST(UiLayoutDirtyGate, GeneratedOwnerReorderReportsChangedThenSettles) {
    Element root;
    root.kind = ElementKind::Canvas;
    Element& items = root.children.emplace_back();
    items.kind = ElementKind::ItemsControl;

    int owner_a = 0;
    int owner_b = 0;
    Element& row_a = items.generated_items.emplace_back();
    row_a.kind = ElementKind::Stack;
    row_a.generated_owner = &owner_a;
    Element& row_b = items.generated_items.emplace_back();
    row_b.kind = ElementKind::Stack;
    row_b.generated_owner = &owner_b;

    ASSERT_TRUE(layout_state_changed(root));
    ASSERT_FALSE(layout_state_changed(root));

    std::swap(items.generated_items[0], items.generated_items[1]);
    EXPECT_TRUE(layout_state_changed(root));
    EXPECT_FALSE(layout_state_changed(root));
}

// generated_items shrinking (an item removed from items_source) must be reported as changed, same
// as growing.
TEST(UiLayoutDirtyGate, GeneratedOwnerRemovalReportsChangedThenSettles) {
    Element root;
    root.kind = ElementKind::Canvas;
    Element& items = root.children.emplace_back();
    items.kind = ElementKind::ItemsControl;

    int owner_a = 0;
    int owner_b = 0;
    Element& row_a = items.generated_items.emplace_back();
    row_a.kind = ElementKind::Stack;
    row_a.generated_owner = &owner_a;
    Element& row_b = items.generated_items.emplace_back();
    row_b.kind = ElementKind::Stack;
    row_b.generated_owner = &owner_b;

    ASSERT_TRUE(layout_state_changed(root));
    ASSERT_FALSE(layout_state_changed(root));

    items.generated_items.pop_back();
    EXPECT_TRUE(layout_state_changed(root));
    EXPECT_FALSE(layout_state_changed(root));
}

// A change several levels deep (Canvas -> Stack -> Label, text changed on the innermost Label) must
// still bubble up to a "changed" result when calling layout_state_changed on the root, not just on
// the changed element itself — the OR-accumulation across the whole recursion is what makes a
// single root call sufficient for prepare_top_canvas/paint_document.
TEST(UiLayoutDirtyGate, DeepGrandchildChangeReportsChangedAtRootThenSettles) {
    Element root;
    root.kind = ElementKind::Canvas;
    Element& stack = root.children.emplace_back();
    stack.kind = ElementKind::Stack;
    Element& label = stack.children.emplace_back();
    label.kind = ElementKind::Label;
    label.text = "Hello";

    ASSERT_TRUE(layout_state_changed(root));
    ASSERT_FALSE(layout_state_changed(root));

    label.text = "Hello, world!";
    EXPECT_TRUE(layout_state_changed(root));
    EXPECT_FALSE(layout_state_changed(root));
}

// Two sibling subtrees must have independent caches: changing one Label's text must not leave the
// other sibling's cache in a state that falsely reports "changed" on the following call, once the
// first Label's change has already been observed and settled.
TEST(UiLayoutDirtyGate, SiblingSubtreesHaveIndependentCaches) {
    Element root;
    root.kind = ElementKind::Canvas;
    // Reserve up front: emplace_back growing the vector later would reallocate and invalidate the
    // label_a/label_b references taken below.
    root.children.reserve(2);
    Element& label_a = root.children.emplace_back();
    label_a.kind = ElementKind::Label;
    label_a.text = "First";
    Element& label_b = root.children.emplace_back();
    label_b.kind = ElementKind::Label;
    label_b.text = "Second";

    ASSERT_TRUE(layout_state_changed(root));
    ASSERT_FALSE(layout_state_changed(root));

    label_a.text = "First, changed";
    ASSERT_TRUE(layout_state_changed(root));

    // No further changes to either sibling: must settle to false, proving label_b's cache was
    // correctly refreshed on the previous call rather than left stale (which could otherwise make
    // this call spuriously report "changed" for label_b even though nothing about it moved).
    EXPECT_FALSE(layout_state_changed(root));
}

// Several consecutive no-change calls must all report false — not just the immediate next call —
// ruling out any hidden flip-flop or a forgotten early-return that only happens to pass a single
// true-then-false check.
TEST(UiLayoutDirtyGate, RepeatedNoChangeCallsStayFalse) {
    Element root;
    root.kind = ElementKind::Canvas;
    Element& label = root.children.emplace_back();
    label.kind = ElementKind::Label;
    label.text = "Hello";

    ASSERT_TRUE(layout_state_changed(root));
    ASSERT_FALSE(layout_state_changed(root));

    EXPECT_FALSE(layout_state_changed(root));
    EXPECT_FALSE(layout_state_changed(root));
    EXPECT_FALSE(layout_state_changed(root));
}

// wind-129 Step 3 smoke tests: prove the gate is actually wired into paint_document (paint.cpp),
// not just that layout_state_changed() works in isolation (covered above). Full regression matrix
// (skip really happens / really doesn't happen for every trigger) is a later step — these two just
// prove the connection exists and doesn't misfire on the two simplest cases.

// Two paint_document() calls with an identical UiPaintInput and an unchanged tree: the second call
// must not disturb anything layout produced — document.layout_computed_once, set true by the first
// (unconditional, since layout_computed_once starts false) call, must stay true, and the child's
// layout_rect must come out identical both times (trivially true if the gate does nothing wrong,
// but worth pinning down explicitly).
TEST(UiLayoutDirtyGate, PaintDocumentRepeatsSameLayoutRectOnIdenticalInput) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack class="box"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;

    const engine::ui::Stylesheet sheet = must_parse_css(".box { width: 50%; height: 50%; }");
    FakePainterStub painter;
    const engine::ui::UiPaintInput input{.canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f}};

    EXPECT_FALSE(document.layout_computed_once);
    engine::ui::paint_document(document, &sheet, painter, input);
    ASSERT_TRUE(document.layout_computed_once);

    Element* box = engine::ui::find_by_kind(document.root, ElementKind::Stack);
    ASSERT_NE(box, nullptr);
    const engine::render::Rect first_rect = box->layout_rect;
    EXPECT_FLOAT_EQ(first_rect.w, 100.0f);

    engine::ui::paint_document(document, &sheet, painter, input);
    EXPECT_TRUE(document.layout_computed_once);
    EXPECT_FLOAT_EQ(box->layout_rect.x, first_rect.x);
    EXPECT_FLOAT_EQ(box->layout_rect.y, first_rect.y);
    EXPECT_FLOAT_EQ(box->layout_rect.w, first_rect.w);
    EXPECT_FLOAT_EQ(box->layout_rect.h, first_rect.h);
}

// Widening canvas_rect between two paint_document() calls must actually re-run layout: a Stack
// with `width: 50%` must pick up the new, wider basis, not keep the layout_rect the first (now
// stale) canvas geometry produced. This is the "skip must NOT fire when something layout-relevant
// changed" half of the gate, exercised through the real paint_document entry point rather than
// layout_state_changed() directly.
TEST(UiLayoutDirtyGate, PaintDocumentRecomputesLayoutRectWhenCanvasRectWidens) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack class="box"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;

    const engine::ui::Stylesheet sheet = must_parse_css(".box { width: 50%; height: 50%; }");
    FakePainterStub painter;

    engine::ui::paint_document(
            document, &sheet, painter, engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f}});
    Element* box = engine::ui::find_by_kind(document.root, ElementKind::Stack);
    ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->layout_rect.w, 100.0f);

    engine::ui::paint_document(
            document, &sheet, painter, engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 400.0f, 100.0f}});
    EXPECT_FLOAT_EQ(box->layout_rect.w, 200.0f);
}

// ---------------------------------------------------------------------------------------------
// wind-129 Крок 4 — regression matrix for the gate wired into paint_document (canvas.cpp's
// prepare_top_canvas follows the identical formula and isn't separately exercised here — see its
// own doc comment in canvas.cpp for why the two call sites are meant to move in lockstep).
//
// Group A: the skip really happens (not just "looks unchanged" by coincidence).
// Group B: the skip does NOT happen when something layout-relevant really changed (one test per
//          trigger listed in Element's/UiDocument's layout_dirty_check_* comments).
// Group C: paint (hover/pressed, keyframe animation, caret blink) is NOT frozen by the gate — it
//          keeps advancing every call regardless of layout_dirty.
// ---------------------------------------------------------------------------------------------

// Group A. Mutates the SAME Stylesheet object in place (no reconstruction — pointer AND
// Stylesheet::generation both unchanged, exactly the one case StyleCacheEntry's own contract
// does NOT promise to catch either — see StyleCacheSkipsRecomputeWhenStylesheetMutatedInPlace-
// WithSamePointer, ui_painter_test.cpp) between repeated, otherwise-identical paint_document()
// calls. If apply_layout_style()+layout() were actually re-run, the new width would show up on
// the very next call; the gate must keep every subsequent call byte-identical to the first.
TEST(UiLayoutDirtyGate, LayoutSkipReallyHappensAcrossRepeatedIdenticalCalls) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack class="box"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;

    engine::ui::Stylesheet sheet = must_parse_css(".box { width: 50px; height: 50px; }");
    FakePainterStub painter;
    const engine::ui::UiPaintInput input{.canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f}};

    engine::ui::paint_document(document, &sheet, painter, input);
    Element* box = engine::ui::find_by_kind(document.root, ElementKind::Stack);
    ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->layout_rect.w, 50.0f);

    ASSERT_EQ(sheet.rules.size(), 1u);
    for (engine::ui::CssDeclaration& decl : sheet.rules[0].declarations) {
        if (decl.property == "width") {
            decl.value = "150px";
        }
    }

    // Three more identical calls: none may pick up the mutated width.
    engine::ui::paint_document(document, &sheet, painter, input);
    EXPECT_FLOAT_EQ(box->layout_rect.w, 50.0f);
    engine::ui::paint_document(document, &sheet, painter, input);
    EXPECT_FLOAT_EQ(box->layout_rect.w, 50.0f);
    engine::ui::paint_document(document, &sheet, painter, input);
    EXPECT_FLOAT_EQ(box->layout_rect.w, 50.0f);
}

// Group B.2 — text change (VM binding or, as here, a direct field write matching how bind_element
// leaves element.text by the time paint_document runs) must recompute a text-hugging element's
// layout_rect. Label has no explicit width, so its layout_rect.w hugs measure_text().
TEST(UiLayoutDirtyGate, TextChangeBetweenPaintCallsRecomputesHugWidth) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="lbl" text="Hi"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;
    const engine::ui::Stylesheet sheet = must_parse_css(".lbl { font-size: 16px; }");
    FakePainterStub painter;
    const engine::ui::UiPaintInput input{.canvas_rect = {0.0f, 0.0f, 400.0f, 100.0f}};

    engine::ui::paint_document(document, &sheet, painter, input);
    Element* label = engine::ui::find_by_kind(document.root, ElementKind::Label);
    ASSERT_NE(label, nullptr);
    const float first_w = label->layout_rect.w;

    label->text = "Hello, this text is a lot longer than before";
    engine::ui::paint_document(document, &sheet, painter, input);
    EXPECT_GT(label->layout_rect.w, first_w);
}

// Group B.3 — a `var(--w)` reference resolved from Element::custom_properties (not gated by
// allow_pseudo — see subject_matches/resolve_var) changing must recompute the width it feeds.
TEST(UiLayoutDirtyGate, CustomPropertyChangeBetweenPaintCallsRecomputesWidth) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack class="box"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;
    const engine::ui::Stylesheet sheet = must_parse_css(".box { width: var(--w, 20px); height: 50px; }");
    FakePainterStub painter;
    const engine::ui::UiPaintInput input{.canvas_rect = {0.0f, 0.0f, 400.0f, 100.0f}};

    Element* box = engine::ui::find_by_kind(document.root, ElementKind::Stack);
    ASSERT_NE(box, nullptr);
    box->custom_properties["w"] = "20px";

    engine::ui::paint_document(document, &sheet, painter, input);
    EXPECT_FLOAT_EQ(box->layout_rect.w, 20.0f);

    box->custom_properties["w"] = "120px";
    engine::ui::paint_document(document, &sheet, painter, input);
    EXPECT_FLOAT_EQ(box->layout_rect.w, 120.0f);
}

namespace {

class DirtyGateRowVm final : public engine::ui::ViewModel {
public:
    DirtyGateRowVm() = default;
};

class DirtyGateListVm final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<DirtyGateRowVm>> items;

    DirtyGateListVm() { property(engine::ui::intern("items"), items); }
};

[[nodiscard]] const void* as_owner(engine::ui::ViewModel* vm) {
    return static_cast<const void*>(vm);
}

}

// Group B.4 — ItemsControl item add/remove/reorder (no scrolling/virtualization involved: this
// control never becomes scrollable, so bind_element's virtualization branch never engages and
// every item is always generated) must be reported as layout-relevant via the generated_owner
// sequence check in layout_state_changed(), and every row must actually be repacked — not left at
// whatever Y position a previous frame's packing produced.
TEST(UiLayoutDirtyGate, ItemsControlAddRemoveReorderRecomputesRowLayoutRects) {
    constexpr float kRowHeight = 20.0f;

    auto a = std::make_shared<DirtyGateRowVm>();
    auto b = std::make_shared<DirtyGateRowVm>();
    engine::ui::ViewModel* vm_a = a.get();
    engine::ui::ViewModel* vm_b = b.get();

    DirtyGateListVm list;
    list.items.set({a, b});

    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <ItemsControl items_source="{binding items}">
            <ItemTemplate><Stack class="row"/></ItemTemplate>
          </ItemsControl>
        </Canvas>
    )",
            nullptr, &list);
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;
    const engine::ui::Stylesheet sheet = must_parse_css(".row { height: 20px; }");
    FakePainterStub painter;
    const engine::ui::UiPaintInput input{.canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f}};

    ASSERT_TRUE(engine::ui::apply_bindings(document, list).has_value());
    engine::ui::paint_document(document, &sheet, painter, input);

    Element* row_a = engine::ui::find_by_generated_owner(document.root, as_owner(vm_a));
    Element* row_b = engine::ui::find_by_generated_owner(document.root, as_owner(vm_b));
    ASSERT_NE(row_a, nullptr);
    ASSERT_NE(row_b, nullptr);
    EXPECT_FLOAT_EQ(row_a->layout_rect.y, 0.0f);
    EXPECT_FLOAT_EQ(row_b->layout_rect.y, kRowHeight);

    // Append a brand-new item c — generated_owner sequence grows 2 -> 3.
    auto c = std::make_shared<DirtyGateRowVm>();
    engine::ui::ViewModel* vm_c = c.get();
    list.items.set({a, b, c});
    ASSERT_TRUE(engine::ui::apply_bindings(document, list).has_value());
    engine::ui::paint_document(document, &sheet, painter, input);
    Element* row_c = engine::ui::find_by_generated_owner(document.root, as_owner(vm_c));
    ASSERT_NE(row_c, nullptr);
    EXPECT_FLOAT_EQ(row_c->layout_rect.y, 2.0f * kRowHeight);

    // Remove a: [b, c] — b and c must both shift up, not stay frozen at their 3-row positions.
    list.items.set({b, c});
    ASSERT_TRUE(engine::ui::apply_bindings(document, list).has_value());
    engine::ui::paint_document(document, &sheet, painter, input);
    row_b = engine::ui::find_by_generated_owner(document.root, as_owner(vm_b));
    row_c = engine::ui::find_by_generated_owner(document.root, as_owner(vm_c));
    ASSERT_NE(row_b, nullptr);
    ASSERT_NE(row_c, nullptr);
    EXPECT_FLOAT_EQ(row_b->layout_rect.y, 0.0f);
    EXPECT_FLOAT_EQ(row_c->layout_rect.y, kRowHeight);

    // Reorder to [c, b]: same set/count as before — only order changed. Must still be reported
    // "changed" (order is layout-relevant on its own) and the rows must swap Y positions.
    list.items.set({c, b});
    ASSERT_TRUE(engine::ui::apply_bindings(document, list).has_value());
    engine::ui::paint_document(document, &sheet, painter, input);
    row_b = engine::ui::find_by_generated_owner(document.root, as_owner(vm_b));
    row_c = engine::ui::find_by_generated_owner(document.root, as_owner(vm_c));
    ASSERT_NE(row_b, nullptr);
    ASSERT_NE(row_c, nullptr);
    EXPECT_FLOAT_EQ(row_c->layout_rect.y, 0.0f);
    EXPECT_FLOAT_EQ(row_b->layout_rect.y, kRowHeight);
}

namespace {

class DirtyGateVirtRowVm final : public engine::ui::ViewModel {
public:
    DirtyGateVirtRowVm() = default;
};

class DirtyGateVirtListVm final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<DirtyGateVirtRowVm>> items;

    DirtyGateVirtListVm() { property(engine::ui::intern("items"), items); }
};

}

// Group B.5 — scrolling a virtualized ItemsControl (wind-127/128) shifts which items are
// generated; layout_state_changed()'s generated_owner-sequence check must catch that and force a
// real relayout, so a newly-windowed-in row gets its true, correctly-packed layout_rect rather than
// whatever stale rect an old Element happened to carry. Row Y is scroll-position-INDEPENDENT by
// design here (scroll is a paint-time pan; the spacer's height exactly compensates for skipped
// rows — see bind_element's "critical invariant" comment, document.cpp): row i always lands at
// i * row_stride, virtualized or not, which is what makes this assertion possible without
// duplicating the windowing math.
TEST(UiLayoutDirtyGate, ItemsControlScrollShiftRecomputesNewlyVisibleRowLayoutRect) {
    constexpr float kRowHeight = 20.0f;
    constexpr float kGap = 2.0f;
    constexpr float kRowStride = kRowHeight + kGap;
    constexpr std::size_t kCount = 40;
    constexpr std::size_t kTargetIndex = 15;

    DirtyGateVirtListVm vm;
    std::vector<std::shared_ptr<DirtyGateVirtRowVm>> rows;
    rows.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        rows.push_back(std::make_shared<DirtyGateVirtRowVm>());
    }
    vm.items.set(rows);

    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <ItemsControl class="list" items_source="{binding items}">
            <ItemTemplate><Canvas class="row"/></ItemTemplate>
          </ItemsControl>
        </Canvas>
    )",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .list { overflow-y: auto; height: 100px; gap: 2px; }
        .row { height: 20px; }
    )");
    FakePainterStub painter;
    const engine::ui::UiPaintInput input{.canvas_rect = {0.0f, 0.0f, 400.0f, 600.0f}};

    // Two warm-up frames: virtualization eligibility needs a previous frame's resolved geometry and
    // sampled row height (bind_element's eligibility comment, document.cpp) — the same two-frame
    // warm-up tests/ui_items_control_virtualization_test.cpp uses.
    ASSERT_TRUE(engine::ui::apply_bindings(document, vm).has_value());
    engine::ui::paint_document(document, &sheet, painter, input);
    ASSERT_TRUE(engine::ui::apply_bindings(document, vm).has_value());
    engine::ui::paint_document(document, &sheet, painter, input);

    engine::ui::ViewModel* target_owner = rows[kTargetIndex].get();
    EXPECT_EQ(engine::ui::find_by_generated_owner(document.root, as_owner(target_owner)), nullptr)
            << "row " << kTargetIndex << " must not be inside the initial (top-of-list) window";

    Element* items_control = engine::ui::find_by_kind(document.root, ElementKind::ItemsControl);
    ASSERT_NE(items_control, nullptr);
    items_control->scroll_y = 300.0f;  // simulate a wheel scroll deep into the list

    ASSERT_TRUE(engine::ui::apply_bindings(document, vm).has_value());
    engine::ui::paint_document(document, &sheet, painter, input);

    Element* target_row = engine::ui::find_by_generated_owner(document.root, as_owner(target_owner));
    ASSERT_NE(target_row, nullptr) << "row " << kTargetIndex << " must now be inside the scrolled window";
    EXPECT_NEAR(target_row->layout_rect.y, static_cast<float>(kTargetIndex) * kRowStride, 0.01f);
}

// Group B.6 — a window-size change that flips a `@media (min-width: ...)` query must recompute
// layout, exactly like tests/ui_painter_test.cpp's MediaMinWidthAppliesAfterResize, but here the
// point is specifically that the SECOND call (the one the gate could otherwise skip) still sees it.
TEST(UiLayoutDirtyGate, WindowSizeChangeAcrossMediaQueryRecomputesLayoutRect) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="title" text="Hi"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .title { height: 20px; }
        @media (min-width: 800) { .title { width: 200px; } }
    )");
    FakePainterStub painter;
    Element* label = engine::ui::find_by_kind(document.root, ElementKind::Label);
    ASSERT_NE(label, nullptr);

    engine::ui::paint_document(document, &sheet, painter,
            engine::ui::UiPaintInput{
                    .canvas_rect = {0.0f, 0.0f, 400.0f, 200.0f}, .window_width = 799.0f, .window_height = 600.0f});
    EXPECT_NE(label->layout_rect.w, 200.0f);

    engine::ui::paint_document(document, &sheet, painter,
            engine::ui::UiPaintInput{
                    .canvas_rect = {0.0f, 0.0f, 400.0f, 200.0f}, .window_width = 800.0f, .window_height = 600.0f});
    EXPECT_FLOAT_EQ(label->layout_rect.w, 200.0f);
}

// Group B.7 — a NEW Stylesheet object (a reload — different pointer, whether or not `generation`
// also differs) must recompute layout: `last_layout_sheet` is compared by pointer.
TEST(UiLayoutDirtyGate, StylesheetReloadWithNewObjectRecomputesLayoutRect) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack class="box"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;
    FakePainterStub painter;
    const engine::ui::UiPaintInput input{.canvas_rect = {0.0f, 0.0f, 400.0f, 100.0f}};
    Element* box = engine::ui::find_by_kind(document.root, ElementKind::Stack);
    ASSERT_NE(box, nullptr);

    const engine::ui::Stylesheet first_sheet = must_parse_css(".box { width: 50px; height: 50px; }");
    engine::ui::paint_document(document, &first_sheet, painter, input);
    EXPECT_FLOAT_EQ(box->layout_rect.w, 50.0f);

    const engine::ui::Stylesheet second_sheet = must_parse_css(".box { width: 150px; height: 50px; }");
    engine::ui::paint_document(document, &second_sheet, painter, input);
    EXPECT_FLOAT_EQ(box->layout_rect.w, 150.0f);
}

// Group B.8 — the very first paint_document() call on a freshly-constructed, entirely empty
// Canvas (no children at all — nothing for layout_state_changed() to see text/custom_properties/
// generated_owner change on) must still run layout, purely because layout_computed_once starts
// false. This is deliberately a SEPARATE, independent path from layout_state_changed() reporting
// "changed" on its own first call (which it does too, via layout_dirty_check_initialized) —
// layout_computed_once exists precisely for a tree shape where that per-element signal alone could
// end up not being the thing forcing the first pass, so this exercises the edge case directly.
TEST(UiLayoutDirtyGate, FirstPaintDocumentCallOnEmptyCanvasAlwaysComputesLayout) {
    engine::ui::UiDocument document;
    document.root.kind = ElementKind::Canvas;
    FakePainterStub painter;
    const engine::ui::UiPaintInput input{.canvas_rect = {10.0f, 20.0f, 300.0f, 150.0f}};

    EXPECT_FALSE(document.layout_computed_once);
    engine::ui::paint_document(document, nullptr, painter, input);
    EXPECT_TRUE(document.layout_computed_once);
    EXPECT_FLOAT_EQ(document.root.layout_rect.x, 10.0f);
    EXPECT_FLOAT_EQ(document.root.layout_rect.y, 20.0f);
    EXPECT_FLOAT_EQ(document.root.layout_rect.w, 300.0f);
    EXPECT_FLOAT_EQ(document.root.layout_rect.h, 150.0f);
}

// Group C.9 — :hover continuing to update the painted background INSTANTLY on an otherwise static
// (layout-unchanged) screen. The CSS here only touches `background` on :hover (no layout-affecting
// property), so this specifically demonstrates the paint-time pseudo-state path is untouched by the
// layout gate, in a test that lives in this file's own context rather than relying only on
// tests/ui_painter_test.cpp's ButtonHoverTogglesAcrossRepeatedPaintCallsWithoutStickyCache (which
// exercises the identical code path but was written before this gate existed).
TEST(UiLayoutDirtyGate, HoverBackgroundStillUpdatesInstantlyWhileLayoutStaysSkipped) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button class="cell" content="X"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        Button { width: 100px; height: 100px; background: #111111; }
        Button:hover { background: #333333; }
    )");
    Element* button = engine::ui::find_by_kind(document.root, ElementKind::Button);
    ASSERT_NE(button, nullptr);

    FakePainterStub idle;
    engine::ui::paint_document(document, &sheet, idle,
            engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 100.0f, 100.0f}, .pointer = {1000.0f, 1000.0f}});
    ASSERT_TRUE(document.layout_computed_once);
    const engine::render::Rect first_rect = button->layout_rect;
    EXPECT_NEAR(idle.last_fill_color.r, 0x11 / 255.0f, 0.01f);

    // Same geometry/tree/stylesheet — only the pointer moved onto the button. layout_rect staying
    // byte-identical is this test's proof that layout_dirty was false on this call (per the isolated
    // PaintDocumentRepeatsSameLayoutRectOnIdenticalInput proof above); the hover background must
    // still flip on this exact call regardless.
    FakePainterStub hover;
    engine::ui::paint_document(document, &sheet, hover,
            engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 100.0f, 100.0f}, .pointer = {10.0f, 10.0f}});
    EXPECT_FLOAT_EQ(button->layout_rect.x, first_rect.x);
    EXPECT_FLOAT_EQ(button->layout_rect.w, first_rect.w);
    EXPECT_NEAR(hover.last_fill_color.r, 0x33 / 255.0f, 0.01f);
}

// Group C.10 — keyframe animation opacity keeps advancing every call on a static, layout-unchanged
// screen, because the motion sample runs inside paint_element(), which paint_document()
// calls unconditionally regardless of the structural layout gate. Opacity does not relayout.
TEST(UiLayoutDirtyGate, KeyframeAnimationOpacityKeepsAdvancingWhileLayoutStaysSkipped) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack class="fade"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .fade { width: 50px; height: 50px; animation-name: fade; animation-duration: 1s; }
        @keyframes fade { from { opacity: 0; } to { opacity: 1; } }
    )");
    Element* box = engine::ui::find_by_kind(document.root, ElementKind::Stack);
    ASSERT_NE(box, nullptr);

    FakePainterStub first;
    engine::ui::paint_document(document, &sheet, first,
            engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f}, .delta_time = 0.5f});
    ASSERT_TRUE(document.layout_computed_once);
    const engine::render::Rect first_rect = box->layout_rect;
    EXPECT_NEAR(first.min_opacity, 0.5f, 0.01f);

    // Second call: identical tree/geometry/stylesheet (layout_rect staying byte-identical is this
    // test's proof layout_dirty was false), yet the animation clock must still have accumulated
    // another 0.5s, reaching the fully-opaque end of the 1s keyframe.
    FakePainterStub second;
    engine::ui::paint_document(document, &sheet, second,
            engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f}, .delta_time = 0.5f});
    EXPECT_FLOAT_EQ(box->layout_rect.x, first_rect.x);
    EXPECT_FLOAT_EQ(box->layout_rect.w, first_rect.w);
    EXPECT_NEAR(second.min_opacity, 1.0f, 0.01f);
}

// Group C.11 — a focused TextInput's caret_blink_timer keeps accumulating every call on a static,
// layout-unchanged screen, because it's advanced inside paint_element() (paint.cpp), unconditional
// on layout_dirty just like the animation case above.
TEST(UiLayoutDirtyGate, CaretBlinkTimerKeepsAdvancingWhileLayoutStaysSkipped) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><TextInput class="input" text="hello"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument& document = *parsed;
    const engine::ui::Stylesheet sheet = must_parse_css(".input { width: 100px; height: 30px; }");
    Element* input_el = engine::ui::find_by_kind(document.root, ElementKind::TextInput);
    ASSERT_NE(input_el, nullptr);
    input_el->focused = true;

    FakePainterStub painter;
    engine::ui::paint_document(document, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f}, .delta_time = 0.3f});
    ASSERT_TRUE(document.layout_computed_once);
    const engine::render::Rect first_rect = input_el->layout_rect;
    EXPECT_NEAR(input_el->caret_blink_timer, 0.3f, 0.001f);
    // fmod(0.3, 1) = 0.3 < 0.5: caret is in its visible half of the blink cycle this call.
    EXPECT_EQ(painter.draw_line_calls, 1);

    // Second call: identical tree/geometry/stylesheet (layout_rect staying byte-identical is this
    // test's proof layout_dirty was false), yet caret_blink_timer must still have accumulated
    // another 0.3s.
    engine::ui::paint_document(document, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f}, .delta_time = 0.3f});
    EXPECT_FLOAT_EQ(input_el->layout_rect.x, first_rect.x);
    EXPECT_FLOAT_EQ(input_el->layout_rect.w, first_rect.w);
    EXPECT_NEAR(input_el->caret_blink_timer, 0.6f, 0.001f);
    // fmod(0.6, 1) = 0.6, not < 0.5: caret is hidden this call, so the cumulative draw_line count
    // (shared FakePainterStub across both calls) must stay at 1, not become 2.
    EXPECT_EQ(painter.draw_line_calls, 1);
}

}
