#include <gtest/gtest.h>

#include "ui/painter.h"
#include "ui_log_painter.h"

#include <engine/render/commands.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// wind-226: the work a frame stopped doing, and the proof that what is drawn did not change.

namespace {

    using engine::render::Rect;
    using engine::ui::Element;
    using engine::ui::ElementKind;
    using ui_test::LogPainter;

    engine::ui::Stylesheet must_parse_css(std::string_view css) {
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(css, warnings);
        EXPECT_TRUE(sheet.has_value());
        return *sheet;
    }

    // --- child_stacking_order ----------------------------------------------------------------------------------------

    std::vector<Element> siblings(const std::vector<int> &z_indices) {
        std::vector<Element> out(z_indices.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i].id = std::to_string(i);
            out[i].z_index = z_indices[i];
        }
        return out;
    }

    std::string order_of(std::vector<Element> &list) {
        std::string out;
        for (const Element *element: engine::ui::child_stacking_order(list)) {
            out += element->id;
        }
        return out;
    }

    TEST(UiStackingOrder, EqualZIndexKeepsDocumentOrder) {
        std::vector<Element> list = siblings({0, 0, 0, 0, 0});
        EXPECT_EQ(order_of(list), "01234");
        std::vector<Element> same = siblings({3, 3, 3});
        EXPECT_EQ(order_of(same), "012");
    }

    TEST(UiStackingOrder, AscendingSiblingsAreAlreadyInPaintOrder) {
        std::vector<Element> list = siblings({-2, 0, 0, 1, 5});
        EXPECT_EQ(order_of(list), "01234");
    }

    TEST(UiStackingOrder, DifferingZIndexSortsStablyByDocumentOrder) {
        // Golden order of the old stable_sort: low first, ties in document order.
        std::vector<Element> list = siblings({2, 0, 1, 0, -1});
        EXPECT_EQ(order_of(list), "41320");
        std::vector<Element> list2 = siblings({1, 1, 0, 1, 0});
        EXPECT_EQ(order_of(list2), "24013");
    }

    TEST(UiStackingOrder, MatchesAStableSortOnRandomZIndices) {
        std::uint32_t state = 12345u;
        const auto next = [&state] {
            state = state * 1664525u + 1013904223u;
            return static_cast<int>((state >> 16) % 4u) - 1;
        };
        for (int round = 0; round < 200; ++round) {
            std::vector<int> z;
            const int count = 1 + static_cast<int>((state >> 8) % 9u);
            for (int i = 0; i < count; ++i) {
                z.push_back(next());
            }
            std::vector<Element> list = siblings(z);
            std::vector<std::size_t> expected(z.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                expected[i] = i;
            }
            std::stable_sort(expected.begin(), expected.end(),
                             [&](std::size_t a, std::size_t b) { return z[a] < z[b]; });
            std::string want;
            for (const std::size_t index: expected) {
                want += std::to_string(index);
            }
            EXPECT_EQ(order_of(list), want) << "round " << round;
        }
    }

    // --- paint state
    // ---------------------------------------------------------------------------------------------------

    struct Fixture {
        engine::ui::UiDocument document;
        engine::ui::Stylesheet sheet;
        LogPainter painter;
    };

    Fixture paint(std::string_view xml, std::string_view css, Rect canvas = {0.0f, 0.0f, 400.0f, 300.0f}) {
        auto parsed = engine::ui::parse_xml(xml);
        EXPECT_TRUE(parsed.has_value());
        Fixture fixture{std::move(*parsed), must_parse_css(css), {}};
        engine::ui::paint_document(fixture.document, &fixture.sheet, fixture.painter,
                                   engine::ui::UiPaintInput{.canvas_rect = canvas});
        return fixture;
    }

    constexpr std::string_view kTreeXml = R"(
    <Canvas>
      <Stack class="a">
        <Stack class="b"/>
        <Stack class="c"/>
      </Stack>
      <Stack class="d">
        <Stack class="e"/>
      </Stack>
      <Stack class="f">
        <Stack class="g"/>
      </Stack>
      <Stack class="h">
        <Stack class="i"/>
      </Stack>
      <Stack class="j">
        <Stack class="k"/>
      </Stack>
      <Stack class="m">
        <Stack class="n"/>
      </Stack>
      <Label class="fits" text="ab"/>
      <Label class="overflows" text="a long label that cannot fit"/>
    </Canvas>
)";

    constexpr std::string_view kTreeCss = R"(
    .a { width: 200; height: 100; background: #110000; }
    .b { width: 50; height: 20; background: #220000; }
    .c { width: 300; height: 20; background: #330000; }
    .d { width: 100; height: 40; background: #001100; border-width: 4; border-color: #00ff00; }
    .e { width: 20; height: 20; background: #002200; }
    .f { width: 80; height: 30; overflow: hidden; background: #000011; }
    .g { width: 200; height: 10; background: #000022; }
    .h { position: relative; width: 60; height: 30; background: #111100; }
    .i { position: absolute; left: 100; top: 50; width: 20; height: 20; background: #222200; }
    .j { width: 50; height: 50; opacity: 0.5; background: #110011; }
    .k { width: 10; height: 10; background: #220022; }
    .m { width: 50; height: 50; opacity: 0.5; background: #001111; }
    .n { width: 10; height: 10; opacity: 0.5; background: #002222; }
    .fits { width: 80; height: 20; background: #aaaa00; }
    .overflows { width: 10; height: 20; white-space: nowrap; background: #00aaaa; }
)";

    glm::vec4 rgb(int r, int g, int b) {
        return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, 1.0f};
    }

    const Element *find_class(const Element &element, std::string_view name) {
        if (std::ranges::find(element.classes, std::string(name)) != element.classes.end()) {
            return &element;
        }
        for (const Element &child: element.children) {
            if (const Element *found = find_class(child, name)) {
                return found;
            }
        }
        return nullptr;
    }

    // Before wind-226 every element drew under the intersection of its own box and every ancestor's, except that an
    // absolute child of an element that does not clip skipped that element's box. This is that rule, from the layout.
    struct OldClip {
        Rect without_own;
        Rect with_own;
    };

    OldClip old_clip(const Element &root, const Element &target, const Rect &canvas) {
        struct Walk {
            const Element *target;
            std::optional<OldClip> found;
            void visit(const Element &element, const Element *parent, OldClip parent_clip) {
                OldClip mine;
                const bool escapes = parent != nullptr && element.position == engine::ui::PositionMode::Absolute &&
                                     !engine::ui::clips_children(*parent);
                if (parent == nullptr) {
                    mine.without_own = parent_clip.with_own;
                } else {
                    mine.without_own = escapes ? parent_clip.without_own : parent_clip.with_own;
                }
                mine.with_own = ui_test::clipped_to(element.layout_rect, mine.without_own);
                if (&element == target) {
                    found = mine;
                }
                for (const Element &child: element.children) {
                    visit(child, &element, mine);
                }
            }
        } walk{&target, std::nullopt};
        walk.visit(root, nullptr, OldClip{canvas, canvas});
        return walk.found.value_or(OldClip{});
    }

    TEST(UiPaintState, EveryFillIsClippedExactlyAsWhenEveryElementScissored) {
        const Rect canvas{0.0f, 0.0f, 400.0f, 300.0f};
        Fixture f = paint(kTreeXml, kTreeCss, canvas);
        const std::map<std::string, glm::vec4> fills = {
                {"a", rgb(0x11, 0, 0)},    {"b", rgb(0x22, 0, 0)},       {"c", rgb(0x33, 0, 0)},
                {"d", rgb(0, 0x11, 0)},    {"e", rgb(0, 0x22, 0)},       {"f", rgb(0, 0, 0x11)},
                {"g", rgb(0, 0, 0x22)},    {"h", rgb(0x11, 0x11, 0)},    {"i", rgb(0x22, 0x22, 0)},
                {"j", rgb(0x11, 0, 0x11)}, {"k", rgb(0x22, 0, 0x22)},    {"m", rgb(0, 0x11, 0x11)},
                {"n", rgb(0, 0x22, 0x22)}, {"fits", rgb(0xaa, 0xaa, 0)}, {"overflows", rgb(0, 0xaa, 0xaa)},
        };
        for (const auto &[name, color]: fills) {
            const Element *element = find_class(f.document.root, name);
            ASSERT_NE(element, nullptr) << name;
            const LogPainter::Call *fill = f.painter.with_color("fill", color);
            ASSERT_NE(fill, nullptr) << name << " drew nothing";
            const Rect drawn_now = ui_test::clipped_to(fill->rect, fill->clipped ? fill->clip : canvas);
            const Rect drawn_before =
                    ui_test::clipped_to(fill->rect, old_clip(f.document.root, *element, canvas).with_own);
            EXPECT_TRUE(ui_test::same_rect(drawn_now, drawn_before))
                    << name << ": now " << drawn_now.x << "," << drawn_now.y << " " << drawn_now.w << "x" << drawn_now.h
                    << ", before " << drawn_before.x << "," << drawn_before.y << " " << drawn_before.w << "x"
                    << drawn_before.h;
        }
    }

    TEST(UiPaintState, TextWiderThanItsBoxIsStillClippedToTheBox) {
        Fixture f = paint(kTreeXml, kTreeCss);
        const Element *overflows = find_class(f.document.root, "overflows");
        ASSERT_NE(overflows, nullptr);
        const LogPainter::Call *text = nullptr;
        for (const LogPainter::Call *call: f.painter.all("text")) {
            if (call->text.starts_with("a long")) {
                text = call;
            }
        }
        ASSERT_NE(text, nullptr);
        ASSERT_TRUE(text->clipped);
        EXPECT_LE(text->clip.w, overflows->layout_rect.w + 0.01f);
    }

    TEST(UiPaintState, ElementsWithNothingToSetEnterNoState) {
        Fixture f = paint(kTreeXml, kTreeCss);
        int elements = 0;
        const auto count = [&](const auto &self, const Element &element) -> void {
            ++elements;
            for (const Element &child: element.children) {
                self(self, child);
            }
        };
        count(count, f.document.root);
        // One save, scissor, and opacity per element before. The elements that fit their box and keep the opacity the
        // painter holds enter nothing, so the calls are fewer than the elements, and every save has its restore.
        EXPECT_LT(f.painter.count("scissor"), elements);
        EXPECT_LT(f.painter.count("save"), elements + 1);
        EXPECT_EQ(f.painter.count("save"), f.painter.count("restore"));
    }

    TEST(UiPaintState, OpacityIsSetWhenItDiffersFromTheOneThePainterHolds) {
        Fixture f = paint(kTreeXml, kTreeCss);
        const LogPainter::Call *j = f.painter.with_color("fill", rgb(0x11, 0, 0x11));
        const LogPainter::Call *k = f.painter.with_color("fill", rgb(0x22, 0, 0x22));
        const LogPainter::Call *m = f.painter.with_color("fill", rgb(0, 0x11, 0x11));
        const LogPainter::Call *n = f.painter.with_color("fill", rgb(0, 0x22, 0x22));
        ASSERT_TRUE(j && k && m && n);
        EXPECT_FLOAT_EQ(j->opacity, 0.5f);
        EXPECT_FLOAT_EQ(k->opacity, 1.0f)
                << "opacity is not inherited: the child sets its own 1 under its parent's 0.5";
        EXPECT_FLOAT_EQ(m->opacity, 0.5f);
        EXPECT_FLOAT_EQ(n->opacity, 0.5f) << "the same 0.5 as the painter holds, set or not";
    }

    TEST(UiPaintState, AnElementWithATransformStillEntersItsState) {
        Fixture f = paint(R"(<Canvas><Stack class="t"/></Canvas>)",
                          ".t { width: 40; height: 40; transform: rotate(10deg); background: #ffffff; }");
        EXPECT_EQ(f.painter.count("transform"), 1);
        const LogPainter::Call *fill = f.painter.with_color("fill", rgb(255, 255, 255));
        ASSERT_NE(fill, nullptr);
    }

    // --- motion overlay
    // ------------------------------------------------------------------------------------------------

    TEST(UiPaintState, AHeldMotionSampleDrawsTheSameAsTheCascade) {
        constexpr std::string_view xml = R"(<Canvas><Label class="x" text="hello"/></Canvas>)";
        constexpr std::string_view base = R"(
        .x { width: 90; height: 30; padding: 2 3; background: #336699; border-radius: 5; border-width: 2;
             border-color: #ff0000; color: #00ff00; text-align: center; align-items: center; font-size: 12; }
    )";
        Fixture still = paint(xml, base);

        // A finished animation that holds its end value leaves a sample on the element, so paint overlays it. The
        // end value is the cascade's own, so every call must come out the same.
        const std::string animated = std::string(base) + R"(
        @keyframes hold { from { opacity: 1; } to { opacity: 1; } }
        .x { animation-name: hold; animation-duration: 0.1s; }
    )";
        auto parsed = engine::ui::parse_xml(xml);
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(animated);
        LogPainter painter;
        for (int frame = 0; frame < 4; ++frame) {
            painter.calls.clear();
            engine::ui::paint_document(
                    *parsed, &sheet, painter,
                    engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 400.f, 300.f}, .delta_time = 0.05f});
        }
        const Element *x = find_class(parsed->root, "x");
        ASSERT_NE(x, nullptr);
        ASSERT_FALSE(x->motion_shown.empty()) << "the fixture must leave a held sample, or it tests nothing";

        ASSERT_EQ(painter.calls.size(), still.painter.calls.size());
        for (std::size_t i = 0; i < painter.calls.size(); ++i) {
            const LogPainter::Call &a = painter.calls[i];
            const LogPainter::Call &b = still.painter.calls[i];
            EXPECT_EQ(a.op, b.op) << i;
            EXPECT_TRUE(ui_test::same_rect(a.rect, b.rect)) << i << " " << a.op;
            EXPECT_EQ(a.color, b.color) << i << " " << a.op;
            EXPECT_EQ(a.text, b.text) << i;
            EXPECT_FLOAT_EQ(a.width, b.width) << i << " " << a.op;
        }
    }

} // namespace
