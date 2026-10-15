#include <gtest/gtest.h>

#include "ui/dirty.h"
#include "ui/painter.h"
#include "ui_log_painter.h"

#include <engine/render/commands.h>
#include <engine/ui/document.h>
#include <engine/ui/paint.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

// wind-232: what a review of steps 2, 4 and 1 found. Each test names the failure it would have shown.

namespace {

    using engine::ui::Element;
    using engine::ui::UiDocument;

    engine::ui::Stylesheet must_parse_css(std::string_view css) {
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(css, warnings);
        EXPECT_TRUE(sheet.has_value());
        return sheet.value_or(engine::ui::Stylesheet{});
    }

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

    void frame(UiDocument &document, engine::ui::ViewModel &vm, const engine::ui::Stylesheet &sheet) {
        ASSERT_TRUE(engine::ui::apply_bindings(document, vm, nullptr, nullptr, &sheet).has_value());
        engine::ui::sync_layout(document, &sheet, nullptr, {0.0f, 0.0f, 400.0f, 300.0f}, 400.0f, 300.0f);
    }

    std::vector<std::string> row_texts(const Element &root) {
        std::vector<std::string> out;
        const auto walk = [&](const auto &self, const Element &e) -> void {
            if (e.generated_owner != nullptr && e.kind == engine::ui::ElementKind::Label) {
                out.push_back(e.text);
            }
            for (const Element &c: e.children) {
                self(self, c);
            }
            for (const Element &c: e.generated_items) {
                self(self, c);
            }
        };
        walk(walk, root);
        return out;
    }

    // Two view-models that live at the same address one after the other, which is what an allocator can hand out.
    struct Slot {
        alignas(RowVm) unsigned char storage[sizeof(RowVm)];
        RowVm *vm = nullptr;
        std::shared_ptr<RowVm> make(std::string text) {
            vm = new (storage) RowVm(std::move(text));
            return std::shared_ptr<RowVm>(vm, [](RowVm *) {});
        }
        void destroy() {
            vm->~RowVm();
            vm = nullptr;
        }
    };

    constexpr char kListXml[] = R"(
    <Canvas><ItemsControl class="list" items_source="{binding items}">
      <ItemTemplate><Label class="t" text="{binding title}"/></ItemTemplate>
    </ItemsControl></Canvas>
)";

    TEST(UiReviewFixes, ARowIsNotReusedForAnItemThatOnlySharesTheAddressOfAnEarlierOne) {
        // Item 1: a row was found by the address of its item, so a new item at a freed item's address showed the old
        // item's text while its binding version happened to match.
        ListVm vm;
        Slot first;
        Slot second;
        auto a = first.make("old");
        auto b = second.make("kept");
        vm.items.set({a, b});
        auto parsed = engine::ui::parse_xml(kListXml, nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(".list { gap: 2px; } .t { width: 60px; height: 14px; }");
        frame(*parsed, vm, sheet);
        frame(*parsed, vm, sheet);
        ASSERT_EQ(row_texts(parsed->root), (std::vector<std::string>{"old", "kept"}));

        // One edit: the first item leaves and a new one is built where it was.
        a.reset();
        first.destroy();
        auto c = first.make("new");
        ASSERT_EQ(static_cast<const void *>(c.get()), static_cast<const void *>(first.storage));
        vm.items.edit([&](auto &items) {
            items.erase(items.begin());
            items.push_back(c);
        });
        frame(*parsed, vm, sheet);
        frame(*parsed, vm, sheet);
        EXPECT_EQ(row_texts(parsed->root), (std::vector<std::string>{"kept", "new"}));
        vm.items.set({});
        frame(*parsed, vm, sheet);
        c.reset();
        b.reset();
        first.destroy();
        second.destroy();
    }

    std::string row_texts_of_label(const Element &root) {
        if (root.kind == engine::ui::ElementKind::Label) {
            return root.text;
        }
        for (const Element &c: root.children) {
            const std::string found = row_texts_of_label(c);
            if (!found.empty()) {
                return found;
            }
        }
        return {};
    }

    class LateVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::string> text;
        LateVm() { text.set("late text"); }
        void register_now() { property(engine::ui::intern("text"), text); }
    };

    TEST(UiReviewFixes, ABindingRegisteredAfterTheFirstPassIsFoundOnTheNext) {
        // Item 2: the table was built once and the failure kept for ever.
        LateVm vm;
        auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="t" text="{binding text}"/></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(".t { width: 80px; height: 14px; }");
        (void) engine::ui::apply_bindings(*parsed, vm, nullptr, nullptr, &sheet);
        vm.register_now();
        frame(*parsed, vm, sheet);
        EXPECT_EQ(row_texts_of_label(parsed->root), "late text");
    }

    class PaintA final : public engine::ui::IPaint {
    public:
        void paint(engine::ui::IDrawList &list, const engine::render::Rect &c) override {
            list.fill_rect({c.x, c.y, 5.0f, 5.0f}, {1, 0, 0, 1}, 0.0f);
        }
        [[nodiscard]] std::uint64_t version() const noexcept override { return 1; }
    };

    class PaintB final : public engine::ui::IPaint {
    public:
        void paint(engine::ui::IDrawList &list, const engine::render::Rect &c) override {
            list.fill_rect({c.x, c.y, 9.0f, 9.0f}, {0, 0, 1, 1}, 0.0f);
        }
        [[nodiscard]] std::uint64_t version() const noexcept override { return 1; }
    };

    class SwapVm final : public engine::ui::ViewModel {
    public:
        PaintA a;
        PaintB b;
        SwapVm() { paint(engine::ui::intern("p"), a); }
        void replace() { paint(engine::ui::intern("p"), b); }
    };

    TEST(UiReviewFixes, AReplacedPaintReachesTheElementAndIsRecordedAgain) {
        // Item 2: `vm.paint(id, other)` never reached `element.paint`, and the picture watch kept a pointer to the old
        // paint.
        SwapVm vm;
        auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack class="g" paint="{binding p}"/></Canvas>)", nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        parsed->verify_layout = false;
        const engine::ui::Stylesheet sheet = must_parse_css(".g { width: 40px; height: 40px; }");
        ui_test::LogPainter painter;
        const auto paint = [&] {
            ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm, nullptr, nullptr, &sheet).has_value());
            painter.calls.clear();
            engine::ui::paint_document(*parsed, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = {0, 0, 200, 200},
                                                                .window_width = 200.0f,
                                                                .window_height = 200.0f});
        };
        paint();
        paint();
        vm.replace();
        paint();
        float width = 0.0f;
        for (const auto &call: painter.calls) {
            if (call.op == "fill") {
                width = call.rect.w;
            }
        }
        EXPECT_FLOAT_EQ(width, 9.0f) << "the new paint drew";
    }

    // --- item 3: what an element with no scissor shows

    TEST(UiReviewFixes, AnAbsoluteGrandchildOutsideItsGrandparentIsStillClippedToIt) {
        // G (does not clip) > P (relative, inside G) > A (absolute, outside G): A used to be cut by G's scissor.
        auto parsed = engine::ui::parse_xml(
                R"(<Canvas><Stack class="g"><Stack class="p"><Stack class="a"/></Stack></Stack></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(
                ".g { width: 100px; height: 100px; } .p { position: relative; width: 50px; height: 50px; }"
                ".a { position: absolute; left: 90px; top: 0px; width: 80px; height: 20px; background: #ff0000; }");
        ui_test::LogPainter painter;
        engine::ui::paint_document(*parsed, &sheet, painter,
                                   engine::ui::UiPaintInput{.canvas_rect = {0, 0, 300, 300},
                                                            .window_width = 300.0f,
                                                            .window_height = 300.0f});
        const ui_test::LogPainter::Call *fill = nullptr;
        for (const auto &call: painter.calls) {
            if (call.op == "fill" && call.color.r > 0.9f && call.color.g < 0.1f) {
                fill = &call;
            }
        }
        ASSERT_NE(fill, nullptr);
        const Element *g = parsed->root.children.empty() ? nullptr : &parsed->root.children[0];
        ASSERT_NE(g, nullptr);
        ASSERT_TRUE(fill->clipped);
        const auto shown = ui_test::clipped_to(fill->rect, fill->clip);
        EXPECT_LE(shown.x + shown.w, g->layout_rect.x + g->layout_rect.w + 0.01f) << "cut at the right edge of G";
    }

    TEST(UiReviewFixes, ASheetWithAHoverTransformKeepsTheScissorOfAPanelAroundIt) {
        // A button that touches its panel's edge and grows on hover used to be cut by the panel only while the pointer
        // was away.
        auto parsed = engine::ui::parse_xml(
                R"(<Canvas><Stack class="panel"><Button class="b" content="x"/></Stack></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(
                ".panel { width: 100px; height: 40px; } .b { width: 100px; height: 40px; background: #00ff00; }"
                ".b:hover { transform: scale(1.2); }");
        ui_test::LogPainter painter;
        engine::ui::paint_document(*parsed, &sheet, painter,
                                   engine::ui::UiPaintInput{.canvas_rect = {0, 0, 300, 300},
                                                            .pointer = {-50, -50},
                                                            .window_width = 300.0f,
                                                            .window_height = 300.0f});
        const ui_test::LogPainter::Call *fill = nullptr;
        for (const auto &call: painter.calls) {
            if (call.op == "fill" && call.color.g > 0.9f && call.color.r < 0.1f) {
                fill = &call;
            }
        }
        ASSERT_NE(fill, nullptr);
        EXPECT_TRUE(fill->clipped) << "the panel keeps its scissor in a sheet where a child can be transformed";
    }

    // --- item 6

    class ZVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<int> z;
        ZVm() { property(engine::ui::intern("z"), z); }
    };

    TEST(UiReviewFixes, AChangedZIndexRecordsThePictureAgain) {
        // Item 6: a restyle that changed only `z-index` marked nothing for paint, so the picture kept the old order.
        ZVm vm;
        auto parsed = engine::ui::parse_xml(
                R"(<Canvas><Stack class="row"><Stack class="a" var-z="{binding z}"/><Stack class="b"/></Stack></Canvas>)",
                nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        parsed->verify_layout = false;
        const engine::ui::Stylesheet sheet = must_parse_css(".row { width: 100px; height: 40px; } .a { width: 40px; "
                                                            "height: 40px; background: #ff0000; z-index: var(--z); }"
                                                            ".b { position: absolute; left: 0px; top: 0px; width: "
                                                            "40px; height: 40px; background: #0000ff; z-index: 1; }");
        ui_test::LogPainter painter;
        const auto paint = [&] {
            ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm, nullptr, nullptr, &sheet).has_value());
            painter.calls.clear();
            engine::ui::paint_document(*parsed, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = {0, 0, 200, 200},
                                                                .window_width = 200.0f,
                                                                .window_height = 200.0f});
        };
        const auto last_fill_is_red = [&] {
            const ui_test::LogPainter::Call *last = nullptr;
            for (const auto &call: painter.calls) {
                if (call.op == "fill" && call.color.a > 0.5f && call.rect.w == 40.0f) {
                    last = &call;
                }
            }
            return last != nullptr && last->color.r > 0.9f;
        };
        vm.z.set(0);
        paint();
        paint();
        EXPECT_FALSE(last_fill_is_red()) << "b (z 1) is on top";
        vm.z.set(5);
        paint();
        EXPECT_TRUE(last_fill_is_red()) << "a (z 5) moved above b";
    }

    // --- item 4

    TEST(UiReviewFixes, ALayoutPassReportsTheElementsItMovedAndOnlyThose) {
        auto parsed = engine::ui::parse_xml(
                R"(<Canvas><Stack class="col"><Label class="a" text="x"/><Label class="b" text="y"/><Stack class="c"/></Stack></Canvas>)");
        ASSERT_TRUE(parsed.has_value());
        const engine::ui::Stylesheet sheet = must_parse_css(
                ".col { width: 100px; gap: 2px; } .a { width: 50px; height: 10px; } .b { width: 50px; height: 10px; }"
                ".c { width: 50px; height: 10px; }");
        engine::ui::sync_layout(*parsed, &sheet, nullptr, {0, 0, 300, 300}, 300.0f, 300.0f);
        Element *a = &parsed->root.children[0].children[0];
        a->height = engine::ui::Length{30.0f, engine::ui::LengthUnit::Px};
        engine::ui::mark_dirty(*parsed, *a, engine::ui::kDirtyStyle | engine::ui::kDirtyLayout, true);
        const std::vector<Element *> moved = [&] {
            engine::ui::MovedElements scope;
            engine::ui::layout(*parsed, {0, 0, 300, 300}, nullptr, true);
            return scope.take();
        }();
        const auto has = [&](const Element *e) { return std::find(moved.begin(), moved.end(), e) != moved.end(); };
        const Element &col = parsed->root.children[0];
        EXPECT_TRUE(has(&col.children[1])) << "the sibling below slid";
        EXPECT_TRUE(has(&col.children[2]));
        EXPECT_FALSE(has(&parsed->root)) << "the root kept its rect";
    }

    TEST(UiReviewFixes, EditUncheckedBumpsTheVersionOnceWithoutComparing) {
        engine::ui::BindableList<int> list;
        list.set({1, 2, 3});
        const std::uint64_t before = list.version();
        list.edit_unchecked([](std::vector<int> &items) { items.push_back(4); });
        EXPECT_EQ(list.version(), before + 1);
        list.edit_unchecked([](std::vector<int> &) {});
        EXPECT_EQ(list.version(), before + 2) << "the caller said it changed";
    }

} // namespace
