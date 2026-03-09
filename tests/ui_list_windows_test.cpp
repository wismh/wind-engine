#include <gtest/gtest.h>

#include "ui/dirty.h"
#include "ui/painter.h"
#include "ui_log_painter.h"

#include <engine/render/commands.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/tree.h>
#include <engine/ui/view_model.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// wind-230: lists whose rows have no fixed height, a shared text measure, a tree whose collapsed branch adds nothing,
// and a Viewport that paints what its camera shows. No window: a recording painter.

namespace {

    using engine::ui::Element;
    using engine::ui::UiDocument;

    class RowVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::string> title;

        explicit RowVm(std::string text) {
            title.set(std::move(text));
            property(engine::ui::intern("title"), title);
        }
    };

    class ListVm final : public engine::ui::ViewModel {
    public:
        engine::ui::BindableList<std::shared_ptr<RowVm>> items;

        ListVm() { property(engine::ui::intern("items"), items); }
    };

    // Row i has (i % 4) + 1 lines of text: 10, 20, 30 or 40 px at font-size 10 without a painter.
    std::vector<std::shared_ptr<RowVm>> make_rows(std::size_t count, std::size_t first = 0) {
        std::vector<std::shared_ptr<RowVm>> rows;
        for (std::size_t i = first; i < first + count; ++i) {
            std::string text = "r";
            for (std::size_t line = 0; line < i % 4; ++line) {
                text += "\nx";
            }
            rows.push_back(std::make_shared<RowVm>(std::move(text)));
        }
        return rows;
    }

    engine::ui::Stylesheet must_parse_css(std::string_view css) {
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(css, warnings);
        EXPECT_TRUE(sheet.has_value());
        return sheet.value_or(engine::ui::Stylesheet{});
    }

    constexpr char kListXml[] = R"(
    <Canvas>
      <ItemsControl class="list" items_source="{binding items}">
        <ItemTemplate><Stack class="row"><Label class="t" text="{binding title}"/></Stack></ItemTemplate>
      </ItemsControl>
    </Canvas>
)";

    // No `height` on the row: its height is its text's.
    constexpr char kListCss[] = R"(
    .list { overflow-y: auto; height: 100px; gap: 2px; }
    .t { font-size: 10px; }
)";

    void frame(UiDocument &document, engine::ui::ViewModel &vm, const engine::ui::Stylesheet &sheet,
               engine::ui::IUiPainter *painter = nullptr) {
        ASSERT_TRUE(engine::ui::apply_bindings(document, vm, nullptr, nullptr, &sheet).has_value());
        const engine::render::Rect canvas{0.0f, 0.0f, 800.0f, 600.0f};
        if (painter != nullptr) {
            engine::ui::paint_document(document, &sheet, *painter,
                                       engine::ui::UiPaintInput{.canvas_rect = canvas,
                                                                .pointer = {-100.0f, -100.0f},
                                                                .window_width = 800.0f,
                                                                .window_height = 600.0f});
            return;
        }
        engine::ui::sync_layout(document, &sheet, nullptr, canvas, 800.0f, 600.0f);
    }

    std::size_t count_elements(const Element &element) {
        std::size_t count = 1;
        for (const Element &child: element.children) {
            count += count_elements(child);
        }
        for (const Element &child: element.generated_items) {
            count += count_elements(child);
        }
        return count;
    }

    std::size_t real_rows(const Element &list) {
        std::size_t count = 0;
        for (const Element &row: list.generated_items) {
            if (row.generated_owner != nullptr) {
                ++count;
            }
        }
        return count;
    }

    // The height the list takes if every row were generated: the sum of its children's heights and gaps.
    float content_height(const Element &list) {
        float total = 0.0f;
        std::size_t shown = 0;
        for (const Element &row: list.generated_items) {
            if (row.display_none) {
                continue;
            }
            total += row.layout_rect.h;
            ++shown;
        }
        return shown == 0 ? 0.0f : total + static_cast<float>(shown - 1) * list.gap.value;
    }

    TEST(UiListWindows, RowsOfNoFixedHeightAreWindowedOnceMeasured) {
        constexpr std::size_t kCount = 10000;
        ListVm vm;
        vm.items.set(make_rows(kCount));
        auto parsed = engine::ui::parse_xml(kListXml, nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);

        frame(*parsed, vm, sheet);
        Element *list = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        ASSERT_NE(list, nullptr);
        EXPECT_EQ(real_rows(*list), kCount) << "the first bind cannot know a height: every row is generated";
        frame(*parsed, vm, sheet);
        frame(*parsed, vm, sheet);

        list = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        EXPECT_LT(real_rows(*list), 30u) << "a window of the viewport plus overscan";
        EXPECT_GT(real_rows(*list), 3u);
        EXPECT_LT(count_elements(parsed->root), 120u);
        EXPECT_NEAR(content_height(*list), 100.0f + 0.0f, 400000.0f) << "spacers stand in for the rest";

        // The whole list is as tall as the rows add up (mean 25 + 2 gap per row), spacers included.
        const float full = list->max_scroll_y + list->layout_rect.h;
        EXPECT_NEAR(full, 27.0f * kCount - 2.0f, 2.0f);
    }

    TEST(UiListWindows, ScrollingMovesTheWindowAndKeepsTheContentHeight) {
        constexpr std::size_t kCount = 2000;
        ListVm vm;
        vm.items.set(make_rows(kCount));
        auto parsed = engine::ui::parse_xml(kListXml, nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);
        for (int i = 0; i < 3; ++i) {
            frame(*parsed, vm, sheet);
        }
        Element *list = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        ASSERT_NE(list, nullptr);
        const float height_before = list->max_scroll_y + list->layout_rect.h;
        const void *first_before = nullptr;
        for (const Element &row: list->generated_items) {
            if (row.generated_owner != nullptr) {
                first_before = row.generated_owner;
                break;
            }
        }
        list->scroll_y = 20000.0f;
        parsed->windows_dirty = true;
        for (int i = 0; i < 3; ++i) {
            frame(*parsed, vm, sheet);
        }
        list = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        const void *first_after = nullptr;
        for (const Element &row: list->generated_items) {
            if (row.generated_owner != nullptr) {
                first_after = row.generated_owner;
                break;
            }
        }
        EXPECT_NE(first_before, first_after) << "the window moved";
        EXPECT_LT(real_rows(*list), 30u);
        EXPECT_NEAR(list->max_scroll_y + list->layout_rect.h, height_before, 1.0f)
                << "measured heights are the ones used";
    }

    TEST(UiListWindows, AnItemOfNoFixedHeightIsScrolledIntoView) {
        ListVm vm;
        vm.items.set(make_rows(2000));
        auto parsed = engine::ui::parse_xml(kListXml, nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);
        for (int i = 0; i < 3; ++i) {
            frame(*parsed, vm, sheet);
        }
        Element *list = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        ASSERT_NE(list, nullptr);
        ASSERT_TRUE(engine::ui::scroll_item_into_view(*parsed, *list, *list, 1500));
        // Item 1500 starts after 375 groups of four rows (10 + 20 + 30 + 40) and 1500 gaps of 2, and is 10 tall.
        EXPECT_NEAR(list->scroll_y, 375.0f * 100.0f + 1500.0f * 2.0f + 10.0f - 100.0f, 1.0f);
    }

    TEST(UiListWindows, AnItemNeverMeasuredCountsAsTheMeanUntilItsRowIsLaidOut) {
        ListVm vm;
        vm.items.set(make_rows(400));
        auto parsed = engine::ui::parse_xml(kListXml, nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);
        for (int i = 0; i < 3; ++i) {
            frame(*parsed, vm, sheet);
        }
        Element *list = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        ASSERT_NE(list, nullptr);
        const float before = list->max_scroll_y + list->layout_rect.h;
        // 100 more items, all four lines tall (40 px) once laid out; until then they count as the mean (25 px).
        auto more = vm.items.get();
        for (int i = 0; i < 100; ++i) {
            more.push_back(std::make_shared<RowVm>("r\nx\nx\nx"));
        }
        vm.items.set(more);
        frame(*parsed, vm, sheet);
        frame(*parsed, vm, sheet);
        list = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        const float estimated = list->max_scroll_y + list->layout_rect.h;
        EXPECT_NEAR(estimated - before, 100.0f * 27.0f, 10.0f);
        // Scroll to the end: those rows are generated, measured, and the height becomes the real one.
        for (int step = 0; step < 6; ++step) {
            list->scroll_y = list->max_scroll_y;
            parsed->windows_dirty = true;
            frame(*parsed, vm, sheet);
            list = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        }
        const float measured = list->max_scroll_y + list->layout_rect.h - before;
        EXPECT_GT(measured, estimated - before + 50.0f) << "the rows laid out are taller than the mean they stood for";
        EXPECT_LT(measured, 100.0f * 42.0f + 1.0f);
    }

    std::vector<std::shared_ptr<RowVm>> repeated_rows(std::size_t count) {
        std::vector<std::shared_ptr<RowVm>> rows;
        for (std::size_t i = 0; i < count; ++i) {
            rows.push_back(std::make_shared<RowVm>("title " + std::to_string(i % 8)));
        }
        return rows;
    }

    TEST(UiListWindows, RowsWithTheSameTextAreMeasuredOnce) {
        ListVm vm;
        vm.items.set(repeated_rows(300));
        auto parsed = engine::ui::parse_xml(kListXml, nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);
        ui_test::LogPainter painter;
        for (int i = 0; i < 3; ++i) {
            frame(*parsed, vm, sheet, &painter);
        }
        EXPECT_LE(painter.measure_calls, 8) << "eight distinct strings at one font and size: one measure each";
        EXPECT_GT(parsed->text_cache.hits, 200u);
    }

    TEST(UiListWindows, ARowThatScrollsBackIsNotShapedAgain) {
        ListVm vm;
        vm.items.set(repeated_rows(300));
        auto parsed = engine::ui::parse_xml(kListXml, nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);
        ui_test::LogPainter painter;
        for (int i = 0; i < 3; ++i) {
            frame(*parsed, vm, sheet, &painter);
        }
        const int shaped = painter.measure_calls;
        Element *list = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
        for (int round = 0; round < 3; ++round) {
            list->scroll_y = 2000.0f;
            parsed->windows_dirty = true;
            for (int i = 0; i < 3; ++i) {
                frame(*parsed, vm, sheet, &painter);
            }
            list->scroll_y = 0.0f;
            parsed->windows_dirty = true;
            for (int i = 0; i < 3; ++i) {
                frame(*parsed, vm, sheet, &painter);
            }
        }
        EXPECT_EQ(painter.measure_calls, shaped);
    }

    // The tree recipe of docs/tech/modules/UI.md#trees: flat rows from `flatten_tree` in a windowed list.
    struct Node {
        int id = 0;
        std::vector<Node> children;
    };

    struct Source {
        [[nodiscard]] int key(const Node *node) const { return node->id; }
        [[nodiscard]] bool has_children(const Node *node) const { return !node->children.empty(); }
        template<typename Emit>
        void for_each_child(const Node *node, Emit &&emit) const {
            for (const Node &child: node->children) {
                emit(&child);
            }
        }
    };

    TEST(UiListWindows, ACollapsedBranchAddsNothingToTheCounts) {
        // Ten roots; the first holds 5000 children.
        std::vector<Node> forest;
        for (int i = 0; i < 10; ++i) {
            forest.push_back(Node{i, {}});
        }
        for (int i = 0; i < 5000; ++i) {
            forest[0].children.push_back(Node{1000 + i, {}});
        }
        std::vector<const Node *> roots;
        for (const Node &node: forest) {
            roots.push_back(&node);
        }
        const auto rows_of = [&](const engine::ui::TreeExpansion<int> &expansion) {
            std::vector<std::shared_ptr<RowVm>> rows;
            (void) engine::ui::flatten_tree(roots, expansion, Source{},
                                            [&rows](const Node *node, const engine::ui::TreeRowInfo &) {
                                                rows.push_back(std::make_shared<RowVm>("n" + std::to_string(node->id)));
                                            });
            return rows;
        };
        const engine::ui::Stylesheet sheet = must_parse_css(kListCss);
        const auto measure = [&](const engine::ui::TreeExpansion<int> &expansion) {
            ListVm vm;
            vm.items.set(rows_of(expansion));
            auto parsed = engine::ui::parse_xml(kListXml, nullptr, &vm);
            EXPECT_TRUE(parsed.has_value());
            for (int i = 0; i < 4; ++i) {
                frame(*parsed, vm, sheet);
            }
            return std::pair{vm.items.get().size(), count_elements(parsed->root)};
        };
        engine::ui::TreeExpansion<int> collapsed;
        collapsed.set_expanded(0, false);
        const auto [collapsed_rows, collapsed_elements] = measure(collapsed);
        EXPECT_EQ(collapsed_rows, 10u);
        const auto [expanded_rows, expanded_elements] = measure(engine::ui::TreeExpansion<int>{});
        EXPECT_EQ(expanded_rows, 5010u);
        // Collapsed: ten rows, all in the window. Expanded: a window of rows and two spacers, nowhere near 5010.
        EXPECT_LT(expanded_elements, 120u);
        EXPECT_LT(collapsed_elements, 80u);
        EXPECT_LE(collapsed_elements, expanded_elements + 20u);
    }

    constexpr char kWorldXml[] = R"(
    <Canvas>
      <Viewport class="world">
        <Stack class="column">
          <Stack class="unit"/>
        </Stack>
      </Viewport>
    </Canvas>
)";

    class CameraVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<float> pan_x;
        engine::ui::Bindable<float> pan_y;
        engine::ui::Bindable<float> zoom;

        CameraVm() {
            zoom.set(1.0f);
            property(engine::ui::intern("pan_x"), pan_x);
            property(engine::ui::intern("pan_y"), pan_y);
            property(engine::ui::intern("zoom"), zoom);
        }
    };

    // A world of 2000 units stacked in a column, each 20 px tall with a 4 px gap, in a 200 x 200 camera.
    UiDocument make_world() {
        auto parsed = engine::ui::parse_xml(kWorldXml);
        EXPECT_TRUE(parsed.has_value());
        UiDocument document = std::move(*parsed);
        Element *column = engine::ui::find_by_kind(document.root, engine::ui::ElementKind::Stack);
        EXPECT_NE(column, nullptr);
        Element unit = column->children.front();
        column->children.clear();
        for (int i = 0; i < 2000; ++i) {
            column->children.push_back(unit);
        }
        document.reindex();
        return document;
    }

    constexpr char kWorldCss[] = R"(
    .world { width: 200px; height: 200px; }
    .column { gap: 4px; }
    .unit { width: 40px; height: 20px; background: #336699; }
)";

    int fill_calls(const ui_test::LogPainter &painter) {
        int count = 0;
        for (const auto &call: painter.calls) {
            if (call.op == "fill") {
                ++count;
            }
        }
        return count;
    }

    TEST(UiListWindows, AViewportPaintsOnlyWhatItsCameraShows) {
        UiDocument document = make_world();
        const engine::ui::Stylesheet sheet = must_parse_css(kWorldCss);
        ui_test::LogPainter painter;
        const auto paint = [&] {
            painter.calls.clear();
            engine::ui::paint_document(document, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 400.0f, 400.0f},
                                                                .pointer = {-100.0f, -100.0f},
                                                                .window_width = 400.0f,
                                                                .window_height = 400.0f});
        };
        paint();
        paint(); // the second frame has the cascade of every unit to know how far each reaches
        Element *world = engine::ui::find_by_kind(document.root, engine::ui::ElementKind::Viewport);
        ASSERT_NE(world, nullptr);
        Element &column = world->children.front();
        // 200 / 24 = 9 units are in the camera; the picture holds half a screen of world more on every side, so a small
        // pan replays it unchanged.
        EXPECT_LT(fill_calls(painter), 32);
        EXPECT_GT(fill_calls(painter), 5);
        EXPECT_GT(column.culled_children, 1900u) << "the rest was skipped";

        // Pan the camera down the column: another window of units, still not the 2000.
        world->pan_y = -24.0f * 1000.0f;
        engine::ui::mark_dirty(document, *world, engine::ui::kDirtyFrame);
        paint();
        EXPECT_LT(fill_calls(painter), 32);
        EXPECT_GT(fill_calls(painter), 5);

        // Zoom out five times: five times as many units fit.
        world->pan_y = 0.0f;
        world->zoom = 0.2f;
        engine::ui::mark_dirty(document, *world, engine::ui::kDirtyFrame);
        paint();
        EXPECT_GT(fill_calls(painter), 40);
        EXPECT_LT(fill_calls(painter), 120);
    }

} // namespace
