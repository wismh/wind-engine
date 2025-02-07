#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// ItemsControl virtualization (bind_element, src/ui/document.cpp): when an ItemsControl scrolls
// itself vertically and its ItemTemplate is a single root Element with a fixed px height (and
// fixed px/absent gap), bind_element generates only the visible window of rows (+overscan) plus
// up to two spacer Elements standing in for the scrolled-past rows, instead of one Element per
// item for every item in items_source. See docs/tech/modules/UI.md's "ItemsControl" section.

namespace {

constexpr float kRowHeight = 20.0f;
constexpr float kGap = 2.0f;
constexpr float kRowStride = kRowHeight + kGap;

class RowVm final : public engine::ui::ViewModel {
public:
    RowVm() = default;
};

class ListVm final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<RowVm>> items;

    ListVm() { property(engine::ui::intern("items"), items); }
};

[[nodiscard]] std::vector<std::shared_ptr<RowVm>> make_rows(std::size_t count) {
    std::vector<std::shared_ptr<RowVm>> rows;
    rows.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        rows.push_back(std::make_shared<RowVm>());
    }
    return rows;
}

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return sheet.value_or(engine::ui::Stylesheet{});
}

// Runs one bind+style+layout pass, matching canvas.cpp's begin_frame ordering (apply_bindings,
// then apply_layout_style, then layout) — virtualization eligibility depends on state
// (overflow_y, max_scroll_y, layout_rect.h, a generated row's resolved height) that only exists
// once a *previous* frame's apply_layout_style + layout has run, so callers run this twice before
// asserting anything about virtualization taking effect.
void run_frame(engine::ui::UiDocument& doc, engine::ui::ViewModel& vm, const engine::ui::Stylesheet& sheet) {
    ASSERT_TRUE(engine::ui::apply_bindings(doc, vm).has_value());
    engine::ui::apply_layout_style(doc.root, &sheet, 800.0f, 600.0f);
    engine::ui::layout(doc, engine::render::Rect{0.0f, 0.0f, 800.0f, 600.0f});
}

// A scrollable ItemsControl (class "list": overflow-y auto, fixed 100px height, 2px gap) with a
// single-root ItemTemplate whose row (class "row") has a fixed 20px height — the eligible case.
constexpr char kEligibleXml[] = R"(
    <Canvas>
      <ItemsControl class="list" items_source="{binding items}">
        <ItemTemplate><Canvas class="row"/></ItemTemplate>
      </ItemsControl>
    </Canvas>
)";

constexpr char kEligibleCss[] = R"(
    .list { overflow-y: auto; height: 100px; gap: 2px; }
    .row { height: 20px; }
)";

[[nodiscard]] std::vector<const engine::ui::Element*> real_items(const engine::ui::Element& items_control) {
    std::vector<const engine::ui::Element*> out;
    for (const engine::ui::Element& child : items_control.generated_items) {
        if (child.generated_owner != nullptr) {
            out.push_back(&child);
        }
    }
    return out;
}

// Sorted set of generated_owner pointers currently in the window, for set-equality/inequality
// comparisons across frames (e.g. "the window didn't change" / "the window did change").
[[nodiscard]] std::vector<const void*> real_item_owners(const engine::ui::Element& items_control) {
    std::vector<const void*> out;
    for (const engine::ui::Element& child : items_control.generated_items) {
        if (child.generated_owner != nullptr) {
            out.push_back(child.generated_owner);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST(UiItemsControlVirtualization, GeneratesOnlyVisibleWindowNotAllItems) {
    constexpr std::size_t kCount = 500;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(kEligibleXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kEligibleCss);

    // Frame 1: establishes layout_rect.h / overflow_y / max_scroll_y / a styled row height —
    // nothing eligible to virtualize on yet (first bind), so this frame still fully generates.
    run_frame(*parsed, vm, sheet);
    engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->generated_items.size(), kCount);

    // Frame 2: virtualization now has everything it needs and kicks in.
    run_frame(*parsed, vm, sheet);
    items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    // ~viewport/row_stride (~5) + 2*overscan (4) rows, plus up to 2 spacers — nowhere near 500.
    EXPECT_LT(items->generated_items.size(), 20u);
    EXPECT_GT(items->generated_items.size(), 0u);
}

TEST(UiItemsControlVirtualization, MaxScrollYMatchesFullGeneration) {
    constexpr std::size_t kCount = 500;
    ListVm vm_virtualized;
    vm_virtualized.items.set(make_rows(kCount));
    auto virtualized = engine::ui::parse_xml(kEligibleXml, nullptr, &vm_virtualized);
    ASSERT_TRUE(virtualized.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kEligibleCss);
    run_frame(*virtualized, vm_virtualized, sheet);
    run_frame(*virtualized, vm_virtualized, sheet); // virtualization active by now
    const engine::ui::Element* virtualized_items =
            engine::ui::find_by_kind(virtualized->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(virtualized_items, nullptr);
    ASSERT_LT(virtualized_items->generated_items.size(), kCount); // sanity: virtualization did trigger

    // Same N, row height and gap, but no overflow-y on the ItemsControl itself — is_scrollable_y()
    // is false, so this one is never eligible and always fully generates. Its max_scroll_y is the
    // ground truth layout_stack's own (unmodified) packing produces for these kCount items.
    ListVm vm_full;
    vm_full.items.set(make_rows(kCount));
    auto full = engine::ui::parse_xml(R"(
        <Canvas>
          <ItemsControl class="list-no-scroll" items_source="{binding items}">
            <ItemTemplate><Canvas class="row"/></ItemTemplate>
          </ItemsControl>
        </Canvas>
    )",
            nullptr, &vm_full);
    ASSERT_TRUE(full.has_value());
    const engine::ui::Stylesheet full_sheet = must_parse_css(R"(
        .list-no-scroll { height: 100px; gap: 2px; }
        .row { height: 20px; }
    )");
    run_frame(*full, vm_full, full_sheet);
    const engine::ui::Element* full_items =
            engine::ui::find_by_kind(full->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(full_items, nullptr);
    ASSERT_EQ(full_items->generated_items.size(), kCount); // sanity: this one never virtualizes

    EXPECT_NEAR(virtualized_items->max_scroll_y, full_items->max_scroll_y, 0.1f);
}

TEST(UiItemsControlVirtualization, WindowMovesWithScroll) {
    constexpr std::size_t kCount = 500;
    ListVm vm;
    std::vector<std::shared_ptr<RowVm>> rows = make_rows(kCount);
    vm.items.set(rows);

    auto parsed = engine::ui::parse_xml(kEligibleXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kEligibleCss);

    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);
    engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    ASSERT_LT(items->generated_items.size(), kCount);

    std::vector<const engine::ui::Element*> window_at_top = real_items(*items);
    ASSERT_FALSE(window_at_top.empty());
    // At scroll_y == 0 the window must start at item 0.
    EXPECT_EQ(window_at_top.front()->generated_owner, rows[0].get());
    // Item 200 (way past a ~9-row window near the top) must not be present.
    const bool row200_at_top = std::any_of(window_at_top.begin(), window_at_top.end(),
            [&](const engine::ui::Element* e) { return e->generated_owner == rows[200].get(); });
    EXPECT_FALSE(row200_at_top);

    // Scroll down by 200 rows worth of stride and re-bind+layout.
    items->scroll_y = 200.0f * kRowStride;
    run_frame(*parsed, vm, sheet);
    items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    std::vector<const engine::ui::Element*> window_after_scroll = real_items(*items);
    ASSERT_FALSE(window_after_scroll.empty());

    // Row 0 must have fallen out of the window; row 200 must now be inside it.
    const bool row0_after_scroll = std::any_of(window_after_scroll.begin(), window_after_scroll.end(),
            [&](const engine::ui::Element* e) { return e->generated_owner == rows[0].get(); });
    const bool row200_after_scroll = std::any_of(window_after_scroll.begin(), window_after_scroll.end(),
            [&](const engine::ui::Element* e) { return e->generated_owner == rows[200].get(); });
    EXPECT_FALSE(row0_after_scroll);
    EXPECT_TRUE(row200_after_scroll);
}

TEST(UiItemsControlVirtualization, ReconciliationPreservedForItemsStayingInWindow) {
    constexpr std::size_t kCount = 500;
    ListVm vm;
    std::vector<std::shared_ptr<RowVm>> rows = make_rows(kCount);
    vm.items.set(rows);

    auto parsed = engine::ui::parse_xml(kEligibleXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kEligibleCss);

    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet); // virtualization active, scroll_y == 0, window starts at row 0
    engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    ASSERT_LT(items->generated_items.size(), kCount);

    // Row 3 is well inside the window at scroll_y == 0 (viewport/row_stride ~= 5, +2 overscan) and
    // stays inside it after a one-row scroll below. Stamp a runtime value onto its Element that
    // only reconciliation (reuse, not a fresh template clone) would preserve across frames.
    engine::ui::Element* row3 = nullptr;
    for (engine::ui::Element& e : items->generated_items) {
        if (e.generated_owner == rows[3].get()) {
            row3 = &e;
            break;
        }
    }
    ASSERT_NE(row3, nullptr);
    row3->animation_elapsed = 0.42f;

    // Scroll down by one row stride: the window shifts by ~1 row but still overlaps heavily, and
    // row 3 stays inside it.
    items->scroll_y = kRowStride;
    run_frame(*parsed, vm, sheet);
    items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);

    row3 = nullptr;
    for (engine::ui::Element& e : items->generated_items) {
        if (e.generated_owner == rows[3].get()) {
            row3 = &e;
            break;
        }
    }
    ASSERT_NE(row3, nullptr) << "row 3 should still be inside the window after a 1-row scroll";
    EXPECT_FLOAT_EQ(row3->animation_elapsed, 0.42f);
}

TEST(UiItemsControlVirtualization, FallbackWithoutOverflowYGeneratesAllItems) {
    constexpr std::size_t kCount = 50;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    // No overflow-y on the ItemsControl: is_scrollable_y() is false, so this never virtualizes.
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <ItemsControl class="list" items_source="{binding items}">
            <ItemTemplate><Canvas class="row"/></ItemTemplate>
          </ItemsControl>
        </Canvas>
    )",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .list { height: 100px; gap: 2px; }
        .row { height: 20px; }
    )");

    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);
    const engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->generated_items.size(), kCount);
}

// Step 2 of wind-128: row height must survive frames whose visible window held zero real
// (non-spacer) rows to sample — the case a wrapping ScrollView (Step 3) opens up by being able
// to scroll this ItemsControl entirely out of view. Without the persistent cache, such a frame
// would find nothing to sample, declare row height unknown, and fall back to full generation.
TEST(UiItemsControlVirtualization, RowHeightCachePersistsAcrossFramesWithEmptyWindow) {
    constexpr std::size_t kCount = 500;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(kEligibleXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kEligibleCss);

    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet); // virtualization active; cache populated from a live sample
    engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    ASSERT_LT(items->generated_items.size(), kCount);
    ASSERT_TRUE(items->virtualization_row_height_cache.has_value());
    EXPECT_FLOAT_EQ(*items->virtualization_row_height_cache, kRowHeight);

    // Simulate several frames in a row whose generated_items (as bind_element would see them
    // coming into the next frame) hold only a spacer — nothing with generated_owner != nullptr
    // to sample a row height from. This stands in for the not-yet-implemented Step 3
    // entirely-offscreen window, without depending on its logic.
    for (int frame = 0; frame < 3; ++frame) {
        engine::ui::Element spacer;
        spacer.kind = engine::ui::ElementKind::Canvas;
        spacer.is_virtualization_spacer = true;
        spacer.height = engine::ui::Length{1234.0f, engine::ui::LengthUnit::Px};
        items->generated_items.clear();
        items->generated_items.push_back(std::move(spacer));

        run_frame(*parsed, vm, sheet);
        items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        ASSERT_NE(items, nullptr);

        // Eligibility still held (small window, not a kCount full-generation fallback), which is
        // only possible if row height came from the cache rather than this frame's (spacer-only)
        // sample; and the cache itself is untouched.
        EXPECT_LT(items->generated_items.size(), kCount);
        ASSERT_TRUE(items->virtualization_row_height_cache.has_value());
        EXPECT_FLOAT_EQ(*items->virtualization_row_height_cache, kRowHeight);
    }
}

TEST(UiItemsControlVirtualization, FallbackWithHorizontalDirectionGeneratesAllItems) {
    constexpr std::size_t kCount = 50;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <ItemsControl class="list" items_source="{binding items}">
            <ItemTemplate><Canvas class="row"/></ItemTemplate>
          </ItemsControl>
        </Canvas>
    )",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .list { overflow-y: auto; height: 100px; gap: 2px; flex-direction: horizontal; }
        .row { height: 20px; }
    )");

    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);
    const engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->generated_items.size(), kCount);
}

TEST(UiItemsControlVirtualization, FallbackWithMultiRootTemplateGeneratesAllItems) {
    constexpr std::size_t kCount = 20;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <ItemsControl class="list" items_source="{binding items}">
            <ItemTemplate>
              <Canvas class="row"/>
              <Canvas class="row2"/>
            </ItemTemplate>
          </ItemsControl>
        </Canvas>
    )",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .list { overflow-y: auto; height: 100px; gap: 2px; }
        .row { height: 20px; }
        .row2 { height: 10px; }
    )");

    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);
    const engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->generated_items.size(), kCount * 2);
}

// Step 3 of wind-128: an ItemsControl that is NOT itself scrollable, but sits (at any nesting
// depth) inside an ancestor that is, virtualizes against that ancestor's viewport instead of
// falling back to full generation. `effective_context` picks up the wrapping ScrollView via
// `scroll_context`, threaded down by Step 1; offset == 0 here since the ItemsControl is the
// ScrollView's only child, so this is Case B's simplest subcase (byte-identical math to Case A
// once offset is folded in).
constexpr char kWrappedEligibleXml[] = R"(
    <Canvas>
      <ScrollView class="scrollview">
        <ItemsControl class="list" items_source="{binding items}">
          <ItemTemplate><Canvas class="row"/></ItemTemplate>
        </ItemsControl>
      </ScrollView>
    </Canvas>
)";

constexpr char kWrappedEligibleCss[] = R"(
    .scrollview { overflow-y: auto; height: 100px; }
    .list { gap: 2px; }
    .row { height: 20px; }
)";

TEST(UiItemsControlVirtualization, WrappedInScrollViewVirtualizesWithOffsetZero) {
    constexpr std::size_t kCount = 50;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(kWrappedEligibleXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kWrappedEligibleCss);

    // Frame 1: bootstrap, nothing eligible yet, full generation.
    run_frame(*parsed, vm, sheet);
    engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->generated_items.size(), kCount);

    // Frame 2: the ItemsControl itself has no overflow-y at all -- is_scrollable_y(element) is
    // false -- so virtualizing here is only possible because effective_context resolved to the
    // wrapping .scrollview ancestor via scroll_context.
    run_frame(*parsed, vm, sheet);
    items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_LT(items->generated_items.size(), kCount);
    EXPECT_GT(items->generated_items.size(), 0u);
}

// Header sibling before the ItemsControl gives it a non-zero offset from the wrapping
// ScrollView's origin, and is tall enough (200px) to fully cover the ScrollView's 100px viewport
// on its own -- at scroll_y == 0 the user sees only the header, and the ItemsControl (starting at
// y == 200 in unscrolled content space) is entirely below the visible area.
constexpr char kWrappedWithHeaderXml[] = R"(
    <Canvas>
      <ScrollView class="scrollview">
        <Canvas class="header"/>
        <ItemsControl class="list" items_source="{binding items}">
          <ItemTemplate><Canvas class="row"/></ItemTemplate>
        </ItemsControl>
      </ScrollView>
    </Canvas>
)";

constexpr char kWrappedWithHeaderCss[] = R"(
    .scrollview { overflow-y: auto; height: 100px; }
    .header { height: 200px; }
    .list { gap: 2px; }
    .row { height: 20px; }
)";

TEST(UiItemsControlVirtualization, EntirelyOffscreenInWrappingScrollViewCollapsesToOneSpacer) {
    constexpr std::size_t kCount = 50;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(kWrappedWithHeaderXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kWrappedWithHeaderCss);

    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet); // scroll_y stays 0: only the header is visible
    const engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);

    // Zero real rows, exactly one spacer standing in for every item.
    ASSERT_EQ(items->generated_items.size(), 1u);
    const engine::ui::Element& spacer = items->generated_items.front();
    EXPECT_EQ(spacer.generated_owner, nullptr);
    ASSERT_TRUE(spacer.height.has_value());
    ASSERT_EQ(spacer.height->unit, engine::ui::LengthUnit::Px);

    // Invariant: the spacer's height must equal N*row_height + (N-1)*gap exactly, or the wrapping
    // ScrollView's own max_scroll_y (and the layout of anything after the ItemsControl) would
    // drift from what full generation would have produced.
    const float expected_total_height =
            static_cast<float>(kCount) * kRowHeight + static_cast<float>(kCount - 1) * kGap;
    EXPECT_NEAR(spacer.height->value, expected_total_height, 0.1f);
}

TEST(UiItemsControlVirtualization, FallbackWithoutFixedPxRowHeightGeneratesAllItems) {
    constexpr std::size_t kCount = 50;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(kEligibleXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    // ".row" never gets a height rule -> its resolved height stays unset (hug), so row_height_px
    // can never be sampled and this ItemsControl can never virtualize.
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .list { overflow-y: auto; height: 100px; gap: 2px; }
    )");

    run_frame(*parsed, vm, sheet);
    run_frame(*parsed, vm, sheet);
    const engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->generated_items.size(), kCount);
}

// --- wind-128 Step 4: tests for Steps 1-3's production logic -----------------------------------

// A 50px header (shorter than the 100px viewport, so it doesn't cover it on its own) gives the
// ItemsControl a non-zero offset from the wrapping ScrollView's origin without pushing it
// entirely out of view. Scrolling to a specific, non-round scroll_y lets us predict the exact
// window `bind_element` must compute if (and only if) it correctly subtracts `offset` from
// `effective_context->scroll_y` (per the Step 3 formula), and distinguish that from what an
// (incorrect) window based on raw, un-adjusted scroll_y would produce.
constexpr char kWrappedWithSmallHeaderXml[] = R"(
    <Canvas>
      <ScrollView class="scrollview">
        <Canvas class="header"/>
        <ItemsControl class="list" items_source="{binding items}">
          <ItemTemplate><Canvas class="row"/></ItemTemplate>
        </ItemsControl>
      </ScrollView>
    </Canvas>
)";

constexpr char kWrappedWithSmallHeaderCss[] = R"(
    .scrollview { overflow-y: auto; height: 100px; }
    .header { height: 50px; }
    .list { gap: 2px; }
    .row { height: 20px; }
)";

TEST(UiItemsControlVirtualization, WindowIndicesAccountForOffsetFromScrollAncestor) {
    constexpr std::size_t kCount = 100;
    constexpr float kHeaderHeight = 50.0f;
    ListVm vm;
    std::vector<std::shared_ptr<RowVm>> rows = make_rows(kCount);
    vm.items.set(rows);

    auto parsed = engine::ui::parse_xml(kWrappedWithSmallHeaderXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kWrappedWithSmallHeaderCss);

    run_frame(*parsed, vm, sheet); // frame 1: bootstrap, full generation
    run_frame(*parsed, vm, sheet); // frame 2: virtualization active, scroll_y == 0

    engine::ui::Element* scrollview = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ScrollView);
    ASSERT_NE(scrollview, nullptr);

    // scroll_y == header_height + 5*row_stride + 5, chosen so that offset (== header_height, since
    // the ItemsControl is the header's only sibling with zero gap between them) subtracted from it
    // gives a non-round scroll_y_effective that yields a window strictly narrower than what the
    // *unadjusted* raw scroll_y would give -- i.e. the two predictions disagree on whether row
    // index 3 and row index 13 are inside the window, which is exactly what lets this test tell a
    // correct offset subtraction apart from a forgotten one.
    scrollview->scroll_y = kHeaderHeight + 5.0f * kRowStride + 5.0f; // == 165.0f
    run_frame(*parsed, vm, sheet); // frame 3: scrolled to a specific, predictable position

    engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    ASSERT_LT(items->generated_items.size(), kCount); // sanity: still virtualizing, not fallback

    const std::vector<const engine::ui::Element*> window = real_items(*items);
    ASSERT_FALSE(window.empty());

    const auto contains_row = [&](std::size_t row_index) {
        return std::any_of(window.begin(), window.end(),
                [&](const engine::ui::Element* e) { return e->generated_owner == rows[row_index].get(); });
    };

    // Correct math (offset subtracted): scroll_y_effective == 115 -> window == rows [3, 12].
    // Row 3 is inside that window but would NOT be inside the window a forgotten-offset
    // calculation (raw scroll_y == 165 -> window [5, 15]) would produce for indices < 5.
    EXPECT_TRUE(contains_row(3)) << "offset (header height) must be subtracted from scroll_y: row 3 "
                                     "is only inside the window once that subtraction happens";
    // Row 13 is outside the correct window (which ends at row 12) but WOULD be inside the
    // forgotten-offset window (which extends to row 15) -- its absence rules out that bug too.
    EXPECT_FALSE(contains_row(13)) << "row 13 would only appear in the window if offset were "
                                       "ignored (i.e. scroll_y used raw, un-adjusted by header height)";
    // Sanity bookends: rows far outside either candidate window are absent either way.
    EXPECT_FALSE(contains_row(0));
    EXPECT_FALSE(contains_row(50));
}

// A Label sibling placed *after* the ItemsControl inside the same wrapping ScrollView must always
// land at the same layout_rect.y -- itemsControl.layout_rect.y + N*row_height + (N-1)*gap -- no
// matter how many real rows `bind_element` actually generated this frame (a handful in a partial
// window, zero in the entirely-offscreen case). layout_rect never depends on scroll (Principles:
// scroll is a paint-time transform), so this is a direct, black-box demonstration of the invariant
// document.cpp's comments describe: the summed used-height of spacers + real rows must always
// equal full-generation's height, since that sum is exactly what the enclosing ScrollView's own
// layout_stack reads back to place whatever comes after the ItemsControl.
constexpr char kWrappedWithFooterXml[] = R"(
    <Canvas>
      <ScrollView class="scrollview">
        <Canvas class="header"/>
        <ItemsControl class="list" items_source="{binding items}">
          <ItemTemplate><Canvas class="row"/></ItemTemplate>
        </ItemsControl>
        <Label class="footer"/>
      </ScrollView>
    </Canvas>
)";

constexpr char kWrappedWithFooterCss[] = R"(
    .scrollview { overflow-y: auto; height: 100px; }
    .header { height: 200px; }
    .list { gap: 2px; }
    .row { height: 20px; }
    .footer { height: 30px; }
)";

TEST(UiItemsControlVirtualization, FooterAfterItemsControlLayoutPositionInvariantAcrossWindowStates) {
    constexpr std::size_t kCount = 100;
    constexpr float kHeaderHeight = 200.0f;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(kWrappedWithFooterXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kWrappedWithFooterCss);

    const float expected_footer_y =
            kHeaderHeight + static_cast<float>(kCount) * kRowHeight + static_cast<float>(kCount - 1) * kGap;

    run_frame(*parsed, vm, sheet); // frame 1: bootstrap, full generation
    run_frame(*parsed, vm, sheet); // frame 2: virtualization active, scroll_y == 0 (header covers
                                    // the whole 100px viewport on its own -> ItemsControl entirely
                                    // offscreen, single spacer, zero real rows -- see the
                                    // EntirelyOffscreenInWrappingScrollViewCollapsesToOneSpacer test)

    const engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    ASSERT_EQ(items->generated_items.size(), 1u); // entirely-offscreen: one spacer, zero real rows

    const engine::ui::Element* footer = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Label);
    ASSERT_NE(footer, nullptr);
    EXPECT_NEAR(footer->layout_rect.y, expected_footer_y, 0.1f);

    // Scroll so the window is partially visible instead (some real rows, some spacer) -- a
    // different generated_items composition than the entirely-offscreen frame above.
    engine::ui::Element* scrollview = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ScrollView);
    ASSERT_NE(scrollview, nullptr);
    scrollview->scroll_y = kHeaderHeight + 5.0f * kRowStride + 5.0f;
    run_frame(*parsed, vm, sheet); // frame 3

    items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_GT(real_items(*items).size(), 0u); // sanity: this frame's composition really differs
    EXPECT_LT(items->generated_items.size(), kCount);

    footer = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Label);
    ASSERT_NE(footer, nullptr);
    // Same footer position as the entirely-offscreen frame, despite a completely different
    // generated_items composition this frame.
    EXPECT_NEAR(footer->layout_rect.y, expected_footer_y, 0.1f);
}

// Two nested independently-scrolling ScrollViews, with an ItemsControl inside the inner one: the
// inner ScrollView must win as `effective_context` (it is the *nearest* scrollable ancestor), even
// though the outer ScrollView is scrollable too and threads its own scroll_context down first.
// `.filler` exists purely to give the outer ScrollView's content enough height to be scrollable on
// its own, independent of whatever the ItemsControl/inner ScrollView are doing.
constexpr char kNestedScrollViewsXml[] = R"(
    <Canvas>
      <ScrollView class="outer">
        <Canvas class="filler"/>
        <ScrollView class="inner">
          <ItemsControl class="list" items_source="{binding items}">
            <ItemTemplate><Canvas class="row"/></ItemTemplate>
          </ItemsControl>
        </ScrollView>
      </ScrollView>
    </Canvas>
)";

constexpr char kNestedScrollViewsCss[] = R"(
    .outer { overflow-y: auto; height: 300px; }
    .filler { height: 250px; }
    .inner { overflow-y: auto; height: 100px; }
    .list { gap: 2px; }
    .row { height: 20px; }
)";

TEST(UiItemsControlVirtualization, NestedScrollViewsUseInnermostAsEffectiveContext) {
    constexpr std::size_t kCount = 100;
    ListVm vm;
    std::vector<std::shared_ptr<RowVm>> rows = make_rows(kCount);
    vm.items.set(rows);

    auto parsed = engine::ui::parse_xml(kNestedScrollViewsXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kNestedScrollViewsCss);

    run_frame(*parsed, vm, sheet); // frame 1: bootstrap, full generation
    run_frame(*parsed, vm, sheet); // frame 2: virtualization active; both outer/inner scroll_y == 0

    engine::ui::Element* outer = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ScrollView);
    ASSERT_NE(outer, nullptr);
    engine::ui::Element* inner = nullptr;
    for (engine::ui::Element& child : outer->children) {
        if (engine::ui::Element* found = engine::ui::find_by_kind(child, engine::ui::ElementKind::ScrollView)) {
            inner = found;
            break;
        }
    }
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(inner, outer);

    engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    ASSERT_LT(items->generated_items.size(), kCount); // sanity: virtualizing already

    const std::vector<const void*> owners_before = real_item_owners(*items);
    ASSERT_FALSE(owners_before.empty());
    // Row 0 is inside the window while both scroll_y's are 0 (offset == 0: the ItemsControl is the
    // inner ScrollView's only child).
    EXPECT_TRUE(std::binary_search(owners_before.begin(), owners_before.end(), rows[0].get()));

    // Changing the OUTER ScrollView's scroll_y (within its own max_scroll_y == 50) must not move
    // the window at all: effective_context resolves to the inner ScrollView, not the outer one, so
    // layout_rect-based offset math never even looks at outer->scroll_y.
    outer->scroll_y = 30.0f;
    run_frame(*parsed, vm, sheet); // frame 3
    items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    const std::vector<const void*> owners_after_outer_scroll = real_item_owners(*items);
    EXPECT_EQ(owners_after_outer_scroll, owners_before);

    // Changing the INNER ScrollView's scroll_y must move the window, as expected for the "nearest"
    // scrollable ancestor.
    inner->scroll_y = 5.0f * kRowStride + 5.0f; // == 115.0f
    run_frame(*parsed, vm, sheet); // frame 4
    items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    const std::vector<const void*> owners_after_inner_scroll = real_item_owners(*items);
    EXPECT_NE(owners_after_inner_scroll, owners_before);
    // Row 0 has scrolled out of the window (window now starts at row 3, per the same offset==0,
    // scroll_y_effective==115 math as the WindowIndicesAccountForOffsetFromScrollAncestor test).
    EXPECT_FALSE(std::binary_search(owners_after_inner_scroll.begin(), owners_after_inner_scroll.end(), rows[0].get()));
}

// Step 2's persistent row-height cache exists specifically because Step 3's *real* mechanism (not
// a manually-injected spacer-only generated_items, as in RowHeightCachePersistsAcrossFramesWithEmptyWindow
// above) can hold the visible window empty for several consecutive frames in a row -- the header
// covers the wrapping ScrollView's whole viewport, so every frame's sample-from-last-frame's-
// generated_items finds only a spacer. This test drives that real mechanism for 3 consecutive
// frames, then scrolls back into view and checks virtualization takes effect on the very next
// frame -- no extra "bootstrap" frame of full generation is needed to re-learn the row height.
TEST(UiItemsControlVirtualization, RowHeightCachePersistsAcrossRealEntirelyOffscreenFramesThenReVirtualizes) {
    constexpr std::size_t kCount = 50;
    constexpr float kHeaderHeight = 200.0f;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(kWrappedWithHeaderXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(kWrappedWithHeaderCss);

    run_frame(*parsed, vm, sheet); // frame 1: bootstrap, full generation -- real rows to sample from
    run_frame(*parsed, vm, sheet); // frame 2: virtualization active; scroll_y == 0 -> genuinely
                                    // entirely offscreen (real Step 3 mechanism, not a manual stub)

    engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    ASSERT_EQ(items->generated_items.size(), 1u); // one spacer, zero real rows, sampled from frame 1
    ASSERT_TRUE(items->virtualization_row_height_cache.has_value());
    EXPECT_FLOAT_EQ(*items->virtualization_row_height_cache, kRowHeight);

    // Two more consecutive real entirely-offscreen frames: generated_items going into each of these
    // holds only the spacer from the previous frame -- nothing for this frame to live-sample.
    for (int frame = 0; frame < 2; ++frame) {
        run_frame(*parsed, vm, sheet);
        items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        ASSERT_NE(items, nullptr);
        ASSERT_EQ(items->generated_items.size(), 1u) << "frame " << frame;
        ASSERT_TRUE(items->virtualization_row_height_cache.has_value()) << "frame " << frame;
        EXPECT_FLOAT_EQ(*items->virtualization_row_height_cache, kRowHeight) << "frame " << frame;
    }

    // Scroll so the list becomes partially visible again, and run exactly one more frame: this
    // frame's row-height sample source (last frame's generated_items) is STILL just the spacer, so
    // eligibility can only survive via the persistent cache, not a live sample.
    engine::ui::Element* scrollview = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ScrollView);
    ASSERT_NE(scrollview, nullptr);
    scrollview->scroll_y = kHeaderHeight + 5.0f * kRowStride + 5.0f;
    run_frame(*parsed, vm, sheet);

    items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_LT(items->generated_items.size(), kCount) << "should virtualize immediately via the cached "
                                                          "row height, not fall back to full generation";
    EXPECT_GT(real_items(*items).size(), 0u);
}

// `has_scroll_context` (Case B eligibility) must not bypass the OTHER, pre-existing eligibility
// conditions that are about the ItemsControl itself (its own `direction`, its row's fixed-px
// height) -- those still gate virtualization independently of where `effective_context` came from.
// `.filler` gives the wrapping ScrollView content taller than its own viewport purely on its own,
// so it is genuinely scrollable (and threads a non-null scroll_context down) regardless of whatever
// the ItemsControl ends up doing.
constexpr char kWrappedFallbackHorizontalXml[] = R"(
    <Canvas>
      <ScrollView class="scrollview">
        <Canvas class="filler"/>
        <ItemsControl class="list" items_source="{binding items}">
          <ItemTemplate><Canvas class="row"/></ItemTemplate>
        </ItemsControl>
      </ScrollView>
    </Canvas>
)";

TEST(UiItemsControlVirtualization, WrappedFallbackWithHorizontalDirectionGeneratesAllItems) {
    constexpr std::size_t kCount = 50;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    auto parsed = engine::ui::parse_xml(kWrappedFallbackHorizontalXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .scrollview { overflow-y: auto; height: 100px; }
        .filler { height: 150px; }
        .list { gap: 2px; flex-direction: horizontal; }
        .row { height: 20px; }
    )");

    run_frame(*parsed, vm, sheet);

    // Confirm the wrapping ScrollView really is scrollable after frame 1 -- i.e. scroll_context
    // will be non-null threaded into frame 2's bind_element for the ItemsControl -- so this test
    // actually exercises "has_scroll_context true but still falls back", not a vacuous case where
    // scroll_context was null all along.
    const engine::ui::Element* scrollview =
            engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ScrollView);
    ASSERT_NE(scrollview, nullptr);
    ASSERT_TRUE(engine::ui::is_scrollable_y(*scrollview));

    run_frame(*parsed, vm, sheet);
    const engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->generated_items.size(), kCount);
}

TEST(UiItemsControlVirtualization, WrappedFallbackWithoutFixedPxRowHeightGeneratesAllItems) {
    constexpr std::size_t kCount = 50;
    ListVm vm;
    vm.items.set(make_rows(kCount));

    // Same wrapping-ScrollView + filler shape as the horizontal-direction fallback test above, but
    // vertical direction and a ".row" with no height rule at all (hug), so row_height_px can never
    // be sampled.
    auto parsed = engine::ui::parse_xml(kWrappedFallbackHorizontalXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .scrollview { overflow-y: auto; height: 100px; }
        .filler { height: 150px; }
        .list { gap: 2px; }
    )");

    run_frame(*parsed, vm, sheet);

    const engine::ui::Element* scrollview =
            engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ScrollView);
    ASSERT_NE(scrollview, nullptr);
    ASSERT_TRUE(engine::ui::is_scrollable_y(*scrollview));

    run_frame(*parsed, vm, sheet);
    const engine::ui::Element* items = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->generated_items.size(), kCount);
}
