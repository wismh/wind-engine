#include <gtest/gtest.h>

#include "ui/dirty.h"
#include "ui/layout_check.h"
#include "ui/painter.h"
#include "ui/pseudo_warning.h"
#include "ui_log_painter.h"

#include <engine/render/commands.h>
#include <engine/ui/command.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// wind-228: versions, dirty flags, pseudo-class invalidation. A quiet frame is asserted with the document's counters
// (slots compared, properties read, passes run) and the per-element ones (cascades computed, times laid out), never
// with a clock.

namespace {

    using engine::ui::Element;
    using engine::ui::UiDocument;
    using ui_test::LogPainter;

    class TestVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::string> title;
        engine::ui::Bindable<std::string> other;
        engine::ui::Bindable<float> wide;
        engine::ui::Bindable<bool> shown;

        TestVm() {
            shown.set(true);
            property(engine::ui::intern("title"), title);
            property(engine::ui::intern("other"), other);
            property(engine::ui::intern("wide"), wide);
            property(engine::ui::intern("shown"), shown);
        }
    };

    engine::ui::Stylesheet must_parse_css(std::string_view css) {
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(css, warnings);
        EXPECT_TRUE(sheet.has_value());
        return *sheet;
    }

    struct Harness {
        UiDocument document;
        engine::ui::Stylesheet sheet;
        TestVm vm;
        LogPainter painter;
        engine::render::Rect canvas{0.0f, 0.0f, 600.0f, 400.0f};

        Harness(std::string_view xml, std::string_view css) : sheet(must_parse_css(css)) {
            auto parsed = engine::ui::parse_xml(xml);
            EXPECT_TRUE(parsed.has_value());
            document = std::move(*parsed);
            // These tests break a mark on purpose in one place; every other layout is checked when the build has the
            // check.
        }

        void frame(glm::vec2 pointer = {-100.0f, -100.0f}, bool down = false) {
            ASSERT_TRUE(engine::ui::apply_bindings(document, vm, nullptr, nullptr, &sheet).has_value());
            painter.calls.clear();
            engine::ui::paint_document(document, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = canvas,
                                                                .pointer = pointer,
                                                                .pointer_down = down,
                                                                .window_width = 600.0f,
                                                                .window_height = 400.0f});
        }

        Element *find(std::string_view name) { return find_in(document.root, name); }

    private:
        static Element *find_in(Element &element, std::string_view name) {
            for (const std::string &cls: element.classes) {
                if (cls == name) {
                    return &element;
                }
            }
            for (Element &child: element.children) {
                if (Element *found = find_in(child, name)) {
                    return found;
                }
            }
            for (Element &child: element.generated_items) {
                if (Element *found = find_in(child, name)) {
                    return found;
                }
            }
            return nullptr;
        }
    };

    // Sums over the whole tree.
    struct Totals {
        std::uint64_t style = 0;
        std::uint64_t layout = 0;
    };

    void add_totals(const Element &element, Totals &totals) {
        totals.style += element.style_rebuilds;
        totals.layout += element.layout_runs;
        for (const Element &child: element.children) {
            add_totals(child, totals);
        }
        for (const Element &child: element.generated_items) {
            add_totals(child, totals);
        }
    }

    Totals totals_of(const UiDocument &document) {
        Totals totals;
        add_totals(document.root, totals);
        return totals;
    }

    constexpr char kTreeXml[] = R"(
    <Canvas>
      <Stack class="page">
        <Stack class="header"><Label class="title" text="{binding title}"/></Stack>
        <Stack class="body">
          <Stack class="row one"><Label class="first" text="one"/></Stack>
          <Stack class="row two"><Label class="second" text="two"/></Stack>
          <Button class="hot" content="go"/>
        </Stack>
        <Stack class="footer"><Label class="note" text="{binding other}"/></Stack>
      </Stack>
    </Canvas>
)";

    constexpr char kTreeCss[] = R"(
    .page { gap: 4; }
    .row { gap: 2; }
    .hot { width: 60; height: 30; background: #223344; }
)";

    TEST(UiDirtyFlags, AQuietFrameDoesNoBindingReadStyleOrLayoutWork) {
        Harness h(kTreeXml, kTreeCss);
        h.vm.title.set("Title");
        h.vm.other.set("Note");
        h.frame();
        h.frame();
        const Totals before = totals_of(h.document);
        const engine::ui::UiWorkCounters counters = h.document.counters;
        ASSERT_GT(before.layout, 0u);

        for (int i = 0; i < 5; ++i) {
            h.frame();
        }
        const Totals after = totals_of(h.document);
        EXPECT_EQ(after.style, before.style) << "no cascade computed";
        EXPECT_EQ(after.layout, before.layout) << "no element laid out";
        EXPECT_EQ(h.document.counters.bound_reads, counters.bound_reads) << "no property read";
        EXPECT_EQ(h.document.counters.table_builds, counters.table_builds);
        EXPECT_EQ(h.document.counters.layout_passes, counters.layout_passes);
        EXPECT_EQ(h.document.counters.restyle_passes, counters.restyle_passes);
        EXPECT_GT(h.document.counters.slots_checked, counters.slots_checked)
                << "one integer per slot is still compared";
        EXPECT_EQ(h.document.counters.table_builds, 1u);
    }

    TEST(UiDirtyFlags, OneChangedStringLaysOutOnlyItsChain) {
        Harness h(kTreeXml, kTreeCss);
        h.vm.title.set("Title");
        h.vm.other.set("Note");
        h.frame();
        h.frame();

        const std::vector<const Element *> chain = {&h.document.root, h.find("page"), h.find("header"),
                                                    h.find("title")};
        const std::vector<const Element *> rest = {h.find("body"),   h.find("row"),  h.find("first"), h.find("second"),
                                                   h.find("footer"), h.find("note"), h.find("hot")};
        std::vector<std::uint32_t> chain_before;
        for (const Element *e: chain) {
            chain_before.push_back(e->layout_runs);
        }
        std::vector<std::uint32_t> rest_before;
        for (const Element *e: rest) {
            rest_before.push_back(e->layout_runs);
        }
        const std::uint64_t style_before = totals_of(h.document).style;

        h.vm.title.set("A much longer title than before");
        h.frame();

        for (std::size_t i = 0; i < chain.size(); ++i) {
            EXPECT_GT(chain[i]->layout_runs, chain_before[i]) << "chain element " << i;
        }
        for (std::size_t i = 0; i < rest.size(); ++i) {
            EXPECT_EQ(rest[i]->layout_runs, rest_before[i]) << "element off the chain " << i;
        }
        EXPECT_EQ(totals_of(h.document).style, style_before) << "a string restyles nothing";
    }

    TEST(UiDirtyFlags, AChangeInsideAFixedSizeElementStopsAtIt) {
        constexpr char kXml[] = R"(
        <Canvas><Stack class="page"><Stack class="fixed"><Label class="inner" text="{binding title}"/></Stack>
        <Label class="sibling" text="s"/></Stack></Canvas>
    )";
        Harness h(kXml, ".fixed { width: 120; height: 40; }");
        h.vm.title.set("a");
        h.frame();
        h.frame();
        const Element *fixed = h.find("fixed");
        const Element *inner = h.find("inner");
        ASSERT_TRUE(engine::ui::is_relayout_boundary(*fixed));
        const std::uint32_t page_runs = h.find("page")->layout_runs;
        const std::uint32_t root_runs = h.document.root.layout_runs;
        const std::uint32_t sibling_runs = h.find("sibling")->layout_runs;
        const std::uint32_t fixed_runs = fixed->layout_runs;
        const std::uint32_t inner_runs = inner->layout_runs;

        h.vm.title.set("a different, longer text");
        h.frame();

        EXPECT_GT(inner->layout_runs, inner_runs);
        EXPECT_GT(fixed->layout_runs, fixed_runs) << "the boundary lays itself out again";
        EXPECT_EQ(h.find("page")->layout_runs, page_runs) << "nothing above the boundary does";
        EXPECT_EQ(h.document.root.layout_runs, root_runs);
        EXPECT_EQ(h.find("sibling")->layout_runs, sibling_runs);
        EXPECT_TRUE(h.document.layout_roots.empty()) << "consumed";
        EXPECT_FLOAT_EQ(fixed->layout_rect.w, 120.0f);
        EXPECT_GT(inner->layout_rect.w, 40.0f);
    }

    TEST(UiDirtyFlags, HoveringAPaintOnlyControlLaysOutNothing) {
        Harness h(kTreeXml, std::string(kTreeCss) + " .hot:hover { background: #ff0000; color: #00ff00; }");
        h.frame();
        h.frame();
        const Element *hot = h.find("hot");
        const engine::render::Rect rect = hot->layout_rect;
        const glm::vec2 on{rect.x + 5.0f, rect.y + 5.0f};
        const Totals before = totals_of(h.document);
        const std::uint64_t passes = h.document.counters.layout_passes;
        const std::uint64_t restyles = h.document.counters.restyle_passes;
        const std::uint32_t hot_style = hot->style_rebuilds;

        h.frame(on);
        EXPECT_TRUE(hot->hovered);
        h.frame(on);
        h.frame({-50.0f, -50.0f});
        EXPECT_FALSE(hot->hovered);

        const Totals after = totals_of(h.document);
        EXPECT_EQ(after.layout, before.layout) << "no element laid out";
        EXPECT_EQ(h.document.counters.layout_passes, passes);
        EXPECT_EQ(h.document.counters.restyle_passes, restyles) << "the layout style never read the paint-only rule";
        // The paint style is rebuilt on enter and on leave, nothing else.
        EXPECT_EQ(hot->style_rebuilds, hot_style + 2);
        EXPECT_EQ(after.style, before.style + 2);
    }

    TEST(UiDirtyFlags, HoveringAControlWithALayoutRuleLaysOutOnEnterAndLeaveOnly) {
        Harness h(kTreeXml, std::string(kTreeCss) + " .hot:hover { padding: 12; }");
        h.frame();
        h.frame();
        const Element *hot = h.find("hot");
        const engine::render::Rect rect = hot->layout_rect;
        const glm::vec2 on{rect.x + 5.0f, rect.y + 5.0f};
        const std::uint64_t passes = h.document.counters.layout_passes;

        h.frame(on);
        EXPECT_EQ(h.document.counters.layout_passes, passes + 1) << "enter";
        const std::uint32_t body_runs = h.find("body")->layout_runs;
        const std::uint32_t row_runs = h.find("row")->layout_runs;
        h.frame(on);
        h.frame(on);
        EXPECT_EQ(h.document.counters.layout_passes, passes + 1) << "a hover that rests lays out nothing";

        h.frame({-50.0f, -50.0f});
        EXPECT_EQ(h.document.counters.layout_passes, passes + 2) << "leave";
        EXPECT_EQ(h.find("row")->layout_runs, row_runs) << "the siblings of the chain never lay out";
        EXPECT_GE(h.find("body")->layout_runs, body_runs);
    }

    TEST(UiDirtyFlags, AnAncestorPseudoRuleRestylesOnlyTheDescendantsItCanMatch) {
        constexpr char kXml[] = R"(
        <Canvas><Stack class="page">
          <Button class="panel" content="">
            <Label class="match" text="m"/>
            <Label class="other" text="o"/>
          </Button>
          <Label class="away" text="a"/>
        </Stack></Canvas>
    )";
        Harness h(kXml, ".panel { width: 100; height: 40; } .panel:hover .match { padding: 6; }");
        h.frame();
        h.frame();
        const Element *panel = h.find("panel");
        const glm::vec2 on{panel->layout_rect.x + 2.0f, panel->layout_rect.y + 2.0f};
        const std::uint32_t match = h.find("match")->style_rebuilds;
        const std::uint32_t other = h.find("other")->style_rebuilds;
        const std::uint32_t away = h.find("away")->style_rebuilds;

        h.frame(on);
        ASSERT_TRUE(panel->hovered);
        EXPECT_GT(h.find("match")->style_rebuilds, match) << "a rule can match it through the hovered ancestor";
        EXPECT_EQ(h.find("other")->style_rebuilds, other) << "no rule can";
        EXPECT_EQ(h.find("away")->style_rebuilds, away);
        EXPECT_FLOAT_EQ(h.find("match")->padding.top.value, 6.0f) << "and it lays out with the padding";
    }

    TEST(UiDirtyFlags, ASheetWithoutAncestorPseudoRulesRestylesNoDescendantOnHover) {
        constexpr char kXml[] = R"(
        <Canvas><Button class="panel" content=""><Label class="inner" text="i"/></Button></Canvas>
    )";
        Harness h(kXml, ".panel { width: 100; height: 40; } .panel:hover { background: #ff0000; }");
        h.frame();
        h.frame();
        const Element *panel = h.find("panel");
        const std::uint32_t inner = h.find("inner")->style_rebuilds;
        h.frame({panel->layout_rect.x + 2.0f, panel->layout_rect.y + 2.0f});
        ASSERT_TRUE(panel->hovered);
        EXPECT_EQ(h.find("inner")->style_rebuilds, inner);
    }

    TEST(UiDirtyFlags, AScrollOrPointerThatDidNotMoveSearchesNoHoverTarget) {
        Harness h(kTreeXml, kTreeCss);
        h.frame();
        h.frame();
        const std::uint64_t epoch = h.document.epoch;
        h.frame();
        h.frame();
        EXPECT_EQ(h.document.epoch, epoch) << "nothing marked, so the hover target was not searched again";
    }

    TEST(UiDirtyFlags, AToggledIfRelaysOutTheParentChain) {
        constexpr char kXml[] = R"(
        <Canvas><Stack class="page"><Label class="a" text="a" if="{binding shown}"/><Label class="b" text="b"/></Stack></Canvas>
    )";
        Harness h(kXml, ".page { gap: 4; }");
        h.frame();
        h.frame();
        const float b_y = h.find("b")->layout_rect.y;
        h.vm.shown.set(false);
        h.frame();
        EXPECT_LT(h.find("b")->layout_rect.y, b_y) << "the sibling closed up";
        h.vm.shown.set(true);
        h.frame();
        EXPECT_FLOAT_EQ(h.find("b")->layout_rect.y, b_y);
    }

    class ToggleCommand final : public engine::ui::ICommand {
    public:
        bool can_execute() const override { return enabled; }
        void execute() override {}
        bool enabled = true;
    };

    class CommandVm final : public engine::ui::ViewModel {
    public:
        ToggleCommand go;
        CommandVm() { command(engine::ui::intern("go"), go); }
    };

    TEST(UiDirtyFlags, ACommandThatBecomesExecutableIsPolledAndMarksPaint) {
        auto parsed =
                engine::ui::parse_xml(R"(<Canvas><Button class="b" command="{binding go}" content="x"/></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        CommandVm vm;
        const engine::ui::Stylesheet sheet =
                must_parse_css(".b { width: 40; height: 20; } .b:disabled { color: #888888; }");
        ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm, nullptr, nullptr, &sheet).has_value());
        Element &button = parsed->root.children[0];
        EXPECT_FALSE(button.disabled);
        engine::ui::clear_dirty(parsed->root, engine::ui::kDirtyPaint);

        vm.go.enabled = false;
        ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm, nullptr, nullptr, &sheet).has_value());
        EXPECT_TRUE(button.disabled);
        EXPECT_NE(button.dirty_self & engine::ui::kDirtyPaint, 0);
        EXPECT_EQ(button.dirty_self & engine::ui::kDirtyLayout, 0) << "a paint-only :disabled rule";
    }

    TEST(UiDirtyFlags, ALayoutWarningNamesTheDocumentAndTheLine) {
        std::string xml = "<Canvas>";
        for (int i = 0; i < 600; ++i) {
            xml += "<Label text=\"x\"/>";
        }
        xml += "</Canvas>";
        auto parsed = engine::ui::parse_xml(xml);
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(".a { color: #fff; }\n.b:hover { color: #000; }\n\n"
                                                            ".c:hover { padding: 4; }\n");
        std::unordered_set<std::string> reported;
        std::vector<std::string> warnings;
        const auto warn = [&](const std::string &message) { warnings.push_back(message); };
        engine::ui::warn_layout_under_pseudo(*parsed, sheet, "0123abcd", reported, warn);
        ASSERT_EQ(warnings.size(), 1u) << "only the rule that lays out";
        EXPECT_NE(warnings[0].find("0123abcd"), std::string::npos);
        EXPECT_NE(warnings[0].find("line 4"), std::string::npos);
        EXPECT_NE(warnings[0].find("padding"), std::string::npos);
        engine::ui::warn_layout_under_pseudo(*parsed, sheet, "0123abcd", reported, warn);
        EXPECT_EQ(warnings.size(), 1u) << "once per rule and document";

        auto small = engine::ui::parse_xml("<Canvas><Label text=\"x\"/></Canvas>");
        ASSERT_TRUE(small.has_value());
        std::unordered_set<std::string> none;
        engine::ui::warn_layout_under_pseudo(*small, sheet, "tiny", none, warn);
        EXPECT_EQ(warnings.size(), 1u) << "a small tree is not worth a warning";
    }

    TEST(UiDirtyFlags, EveryPseudoClassThatLaysOutMovesTheBoxAndRestoresIt) {
        constexpr char kXml[] = R"(
        <Canvas><Stack class="row">
          <Button class="a" content="go"/>
          <Label class="after" text="after"/>
        </Stack></Canvas>
    )";
        constexpr char kCss[] = R"(
        .row { direction: horizontal; flex-direction: horizontal; gap: 3; }
        .a { width: 50; height: 20; }
        .a:hover { width: 61; }
        .a:pressed { width: 62; }
        .a:focus { width: 63; }
        .a:disabled { width: 64; }
        .a:checked { width: 65; }
    )";
        Harness h(kXml, kCss);
        h.frame();
        h.frame();
        Element *a = h.find("a");
        const float base = a->layout_rect.w;
        ASSERT_FLOAT_EQ(base, 50.0f);
        const float after_x = h.find("after")->layout_rect.x;
        struct Case {
            std::uint8_t bit;
            float width;
        };
        for (const Case c: {Case{engine::ui::kPseudoHover, 61.0f}, Case{engine::ui::kPseudoPressed, 62.0f},
                            Case{engine::ui::kPseudoFocus, 63.0f}, Case{engine::ui::kPseudoDisabled, 64.0f},
                            Case{engine::ui::kPseudoChecked, 65.0f}}) {
            engine::ui::set_pseudo(h.document, *a, c.bit, true, &h.sheet);
            h.frame();
            EXPECT_FLOAT_EQ(a->layout_rect.w, c.width) << "bit " << static_cast<int>(c.bit);
            EXPECT_GT(h.find("after")->layout_rect.x, after_x) << "the sibling moved over";
            engine::ui::set_pseudo(h.document, *a, c.bit, false, &h.sheet);
            h.frame();
            EXPECT_FLOAT_EQ(a->layout_rect.w, base) << "bit " << static_cast<int>(c.bit);
            EXPECT_FLOAT_EQ(h.find("after")->layout_rect.x, after_x);
        }
    }

    TEST(UiDirtyFlags, ASubtreeShownAgainIsLaidOutAgainWhateverChangedWhileItWasHidden) {
        constexpr char kXml[] = R"(
        <Canvas><Stack class="page">
          <Stack class="body" if="{binding shown}">
            <Stack class="inner"><Label class="deep" text="{binding title}"/></Stack>
          </Stack>
          <Label class="tail" text="tail"/>
        </Stack></Canvas>
    )";
        Harness h(kXml, ".page { gap: 2; }");
        h.vm.title.set("x");
        h.frame();
        h.frame();
        const float width_before = h.find("deep")->layout_rect.w;
        ASSERT_GT(width_before, 0.0f);

        h.vm.shown.set(false);
        h.frame();
        EXPECT_TRUE(h.find("body")->display_none);
        // While it is hidden its text changes and a row would be rebuilt: nothing below is laid out.
        h.vm.title.set("a text that is a good deal longer than before");
        h.frame();
        h.frame();

        h.vm.shown.set(true);
        h.frame();
        EXPECT_FALSE(h.find("body")->display_none);
        EXPECT_GT(h.find("deep")->layout_rect.w, width_before) << "the hidden change is in the rect";
        EXPECT_GT(h.find("deep")->layout_rect.h, 0.0f);
    }

    TEST(UiDirtyFlags, TheLayoutCheckFindsAChangeThatNeverMarkedWhatItFeeds) {
        if (!engine::ui::kUiLayoutCheckBuilt) {
            GTEST_SKIP() << "this build has no ENGINE_UI_LAYOUT_CHECK";
        }
        Harness h(kTreeXml, kTreeCss);
        h.vm.title.set("Title");
        h.frame();
        h.frame();
        h.document.abort_on_layout_mismatch = false;
        EXPECT_EQ(h.document.counters.layout_check_failures, 0u);
        EXPECT_GT(h.document.counters.layout_checks, 0u);

        // Text edited in place, nothing marked: layout skips, and the copy laid out from scratch says it owed work.
        h.find("title")->text = "A text edited behind the engine's back, far wider than before";
        h.frame();
        h.frame();
        EXPECT_GT(h.document.counters.layout_check_failures, 0u);
    }

} // namespace
