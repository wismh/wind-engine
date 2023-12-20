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
