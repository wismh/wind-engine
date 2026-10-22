#include <gtest/gtest.h>

#include "ui/dirty.h"
#include "ui/painter.h"
#include "ui/picture.h"
#include "ui_log_painter.h"

#include <engine/render/commands.h>
#include <engine/ui/document.h>
#include <engine/ui/paint.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// wind-231: repaint boundaries keep a Picture (a list of painter commands) and replay it while nothing they show
// changed. The painter here records; nothing needs a window.

namespace {

    using engine::ui::Element;
    using engine::ui::Picture;
    using engine::ui::UiDocument;

    class HudVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::string> title;
        engine::ui::Bindable<std::string> note;

        HudVm() {
            title.set("title");
            note.set("note");
            property(engine::ui::intern("title"), title);
            property(engine::ui::intern("note"), note);
        }
    };

    engine::ui::Stylesheet must_parse_css(std::string_view css) {
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(css, warnings);
        EXPECT_TRUE(sheet.has_value());
        return sheet.value_or(engine::ui::Stylesheet{});
    }

    struct Harness {
        UiDocument document;
        engine::ui::Stylesheet sheet;
        HudVm vm;
        ui_test::LogPainter painter;
        engine::render::Rect canvas{0.0f, 0.0f, 400.0f, 300.0f};

        Harness(std::string_view xml, std::string_view css) : sheet(must_parse_css(css)) {
            auto parsed = engine::ui::parse_xml(xml, nullptr, &vm);
            EXPECT_TRUE(parsed.has_value());
            document = std::move(*parsed);
        }

        void frame(float dt = 0.016f) {
            ASSERT_TRUE(engine::ui::apply_bindings(document, vm, nullptr, nullptr, &sheet).has_value());
            painter.calls.clear();
            engine::ui::paint_document(document, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = canvas,
                                                                .pointer = {-100.0f, -100.0f},
                                                                .delta_time = dt,
                                                                .window_width = 400.0f,
                                                                .window_height = 300.0f});
        }

        Element *find(std::string_view cls) { return find_in(document.root, cls); }

    private:
        static Element *find_in(Element &element, std::string_view cls) {
            for (const std::string &name: element.classes) {
                if (name == cls) {
                    return &element;
                }
            }
            for (Element &child: element.children) {
                if (Element *found = find_in(child, cls)) {
                    return found;
                }
            }
            for (Element &child: element.generated_items) {
                if (Element *found = find_in(child, cls)) {
                    return found;
                }
            }
            return nullptr;
        }
    };

    constexpr char kHudXml[] = R"(
    <Canvas>
      <Stack class="hud">
        <Stack class="bar"><Label class="title" text="{binding title}"/></Stack>
        <Stack class="body">
          <Label class="plain" text="plain"/>
          <Label class="live" text="{binding note}"/>
          <Stack class="chip"/>
        </Stack>
      </Stack>
    </Canvas>
)";

    constexpr char kHudCss[] = R"(
    .hud { gap: 4px; background: #101820; }
    .bar { height: 24px; background: #203040; }
    .body { gap: 2px; }
    .title, .plain { font-size: 12px; width: 80px; height: 16px; }
    .live { font-size: 12px; width: 80px; height: 16px; repaint-boundary: yes; }
    .chip { width: 20px; height: 20px; background: #884422; border-radius: 4px; }
)";

    // The commands a canvas issued on the last frame, as one comparable string.
    std::string issued(const ui_test::LogPainter &painter) {
        std::string out;
        for (const auto &call: painter.calls) {
            out += call.op + "(" + std::to_string(call.rect.x) + "," + std::to_string(call.rect.y) + "," +
                   std::to_string(call.rect.w) + "," + std::to_string(call.rect.h) + "," + call.text + ")\n";
        }
        return out;
    }

    TEST(UiPicture, ARecordedPictureReplaysTheSameCallsOnAnotherPainter) {
        Harness h(kHudXml, kHudCss);
        h.frame();
        const std::string direct = issued(h.painter);
        ASSERT_FALSE(direct.empty());

        // The root's frame, replayed on a fresh recording painter, is what the canvas drew.
        Picture copy;
        ui_test::LogPainter inner;
        engine::ui::RecordingPainter recording(inner, copy);
        h.document.root.picture.frame->replay(recording);
        ui_test::LogPainter replayed;
        copy.replay(replayed);
        std::string again;
        for (const auto &call: replayed.calls) {
            again += call.op + "(" + std::to_string(call.rect.x) + "," + std::to_string(call.rect.y) + "," +
                     std::to_string(call.rect.w) + "," + std::to_string(call.rect.h) + "," + call.text + ")\n";
        }
        // The canvas also saves and sets the scissor around the root; the picture holds the root's own commands.
        EXPECT_NE(again.find("fill"), std::string::npos);
        EXPECT_NE(direct.find(again.substr(0, again.find('\n'))), std::string::npos);
    }

    TEST(UiPicture, AQuietFrameReplaysAndWalksNoElement) {
        Harness h(kHudXml, kHudCss);
        h.frame();
        h.frame();
        const std::string first = issued(h.painter);
        const auto counters = h.document.counters;
        const std::shared_ptr<Picture> chrome = h.document.root.picture.chrome.picture;
        const std::shared_ptr<Picture> content = h.document.root.picture.content.picture;
        const Picture chrome_before = *chrome;
        const Picture content_before = *content;
        ASSERT_NE(chrome, nullptr);
        ASSERT_NE(content, nullptr);

        for (int i = 0; i < 5; ++i) {
            h.frame();
            EXPECT_EQ(issued(h.painter), first) << "the same commands every frame";
        }
        EXPECT_EQ(h.document.counters.painted_elements, counters.painted_elements) << "no element was painted";
        EXPECT_EQ(h.document.counters.pictures_recorded, counters.pictures_recorded) << "nothing was recorded";
        EXPECT_EQ(h.document.counters.pictures_replayed, counters.pictures_replayed + 5);
        EXPECT_EQ(h.document.root.picture.chrome.picture, chrome);
        EXPECT_EQ(h.document.root.picture.content.picture, content);
        EXPECT_TRUE(*chrome == chrome_before) << "the recorded command list is identical across the frames";
        EXPECT_TRUE(*content == content_before);
    }

    TEST(UiPicture, OneChangedLabelRecordsOnlyItsOwnBoundary) {
        Harness h(kHudXml, kHudCss);
        h.frame();
        h.frame();
        Element *live = h.find("live");
        ASSERT_NE(live, nullptr);
        const Picture root_content = *h.document.root.picture.content.picture;
        const Picture root_chrome = *h.document.root.picture.chrome.picture;
        const Picture live_chrome = *live->picture.chrome.picture;
        const auto before = h.document.counters;

        h.vm.note.set("changed");
        h.frame();

        EXPECT_FALSE(*live->picture.chrome.picture == live_chrome) << "the label's picture holds the new text";
        EXPECT_TRUE(*h.document.root.picture.content.picture == root_content) << "the screen's picture is untouched";
        EXPECT_TRUE(*h.document.root.picture.chrome.picture == root_chrome);
        EXPECT_EQ(h.document.counters.pictures_recorded - before.pictures_recorded, 2u)
                << "the label's chrome and content";
        bool drew_new_text = false;
        for (const auto &call: h.painter.calls) {
            drew_new_text = drew_new_text || call.text == "changed";
        }
        EXPECT_TRUE(drew_new_text);
        // And it settles: the next frame replays everything.
        const auto after = h.document.counters;
        h.frame();
        EXPECT_EQ(h.document.counters.pictures_recorded, after.pictures_recorded);
    }

    TEST(UiPicture, ATextChangeInsideThePlainRegionRecordsTheBoundaryThatHoldsIt) {
        Harness h(kHudXml, kHudCss);
        h.frame();
        h.frame();
        const Picture root_content = *h.document.root.picture.content.picture;
        h.vm.title.set("a different title");
        h.frame();
        EXPECT_FALSE(*h.document.root.picture.content.picture == root_content);
        bool drew = false;
        for (const auto &call: h.painter.calls) {
            drew = drew || call.text == "a different title";
        }
        EXPECT_TRUE(drew);
    }

    TEST(UiPicture, AResizedCanvasRecordsAgain) {
        Harness h(kHudXml, kHudCss);
        h.frame();
        h.frame();
        const auto before = h.document.counters;
        h.canvas = {0.0f, 0.0f, 500.0f, 300.0f};
        h.frame();
        EXPECT_GT(h.document.counters.pictures_recorded, before.pictures_recorded);
    }

    constexpr char kScrollXml[] = R"(
    <Canvas>
      <Stack class="list">
        <Stack class="row"/><Stack class="row"/><Stack class="row"/><Stack class="row"/><Stack class="row"/>
        <Stack class="row"/><Stack class="row"/><Stack class="row"/><Stack class="row"/><Stack class="row"/>
      </Stack>
    </Canvas>
)";

    TEST(UiPicture, AScrollOffsetIsAParameterOfTheFrameNotOfThePicture) {
        Harness h(kScrollXml, ".list { overflow-y: scroll; width: 100px; height: 60px; gap: 2px; }\n"
                              ".row { height: 20px; background: #446688; }");
        h.frame();
        h.frame();
        Element *list = h.find("list");
        ASSERT_NE(list, nullptr);
        ASSERT_GT(list->max_scroll_y, 0.0f);
        const auto before = h.document.counters;
        const std::string unscrolled = issued(h.painter);

        list->scroll_y = 30.0f;
        engine::ui::mark_dirty(h.document, *list, engine::ui::kDirtyFrame);
        h.frame();

        EXPECT_EQ(h.document.counters.pictures_recorded, before.pictures_recorded) << "scrolling rewrote no picture";
        EXPECT_EQ(h.document.counters.painted_elements, before.painted_elements);
        bool scrolled_view = false;
        for (const auto &call: h.painter.calls) {
            scrolled_view = scrolled_view || call.op == "view";
        }
        EXPECT_TRUE(scrolled_view);
        EXPECT_NE(issued(h.painter), unscrolled);
    }

    class CountingPaint final : public engine::ui::IPaint {
    public:
        void paint(engine::ui::IDrawList &list, const engine::render::Rect &content) override {
            ++paints;
            list.fill_rect(engine::render::Rect{content.x, content.y, 10.0f, 10.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, 0.0f);
        }
        [[nodiscard]] std::uint64_t version() const noexcept override { return current; }
        std::uint64_t current = 1;
        int paints = 0;
    };

    class UnversionedPaint final : public engine::ui::IPaint {
    public:
        void paint(engine::ui::IDrawList &list, const engine::render::Rect &content) override {
            ++paints;
            list.fill_rect(engine::render::Rect{content.x, content.y, 10.0f, 10.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, 0.0f);
        }
        int paints = 0;
    };

    class PaintVm final : public engine::ui::ViewModel {
    public:
        CountingPaint versioned;
        UnversionedPaint unversioned;
        PaintVm() {
            paint(engine::ui::intern("a"), versioned);
            paint(engine::ui::intern("b"), unversioned);
        }
    };

    TEST(UiPicture, AnIPaintWithAVersionIsDrawnAgainOnlyWhenTheVersionMoves) {
        PaintVm vm;
        auto parsed = engine::ui::parse_xml(
                R"(<Canvas><Stack class="g"><Stack class="a" paint="{binding a}"/></Stack></Canvas>)", nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        parsed->verify_layout = false; // the check paints a second time and these tests count paints
        const engine::ui::Stylesheet sheet =
                must_parse_css(".g { width: 100px; height: 100px; } .a { width: 50px; height: 50px; }");
        ui_test::LogPainter painter;
        const auto frame = [&] {
            ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm, nullptr, nullptr, &sheet).has_value());
            engine::ui::paint_document(*parsed, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 200.0f, 200.0f},
                                                                .window_width = 200.0f,
                                                                .window_height = 200.0f});
        };
        frame();
        frame();
        EXPECT_EQ(vm.versioned.paints, 1) << "the second frame replayed";
        for (int i = 0; i < 4; ++i) {
            frame();
        }
        EXPECT_EQ(vm.versioned.paints, 1);
        vm.versioned.current = 2;
        frame();
        EXPECT_EQ(vm.versioned.paints, 2) << "a new version records again";
        frame();
        EXPECT_EQ(vm.versioned.paints, 2);
    }

    TEST(UiPicture, AnIPaintWithNoVersionIsDrawnEveryFrame) {
        PaintVm vm;
        auto parsed = engine::ui::parse_xml(
                R"(<Canvas><Stack class="g"><Stack class="a" paint="{binding b}"/></Stack></Canvas>)", nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        parsed->verify_layout = false; // the check paints a second time and these tests count paints
        const engine::ui::Stylesheet sheet =
                must_parse_css(".g { width: 100px; height: 100px; } .a { width: 50px; height: 50px; }");
        ui_test::LogPainter painter;
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm, nullptr, nullptr, &sheet).has_value());
            engine::ui::paint_document(*parsed, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 200.0f, 200.0f},
                                                                .window_width = 200.0f,
                                                                .window_height = 200.0f});
        }
        EXPECT_EQ(vm.unversioned.paints, 5);
    }

    TEST(UiPicture, AFocusedTextInputBlinksSoItsPictureIsNeverReplayed) {
        Harness h(R"(<Canvas><TextInput class="in" text="hello"/></Canvas>)", ".in { width: 100px; height: 24px; }");
        Element *input = h.find("in");
        ASSERT_NE(input, nullptr);
        input->focused = true;
        h.frame(0.3f);
        const float timer = input->caret_blink_timer;
        h.frame(0.3f);
        EXPECT_GT(input->caret_blink_timer, timer) << "the clock still runs";
    }

    TEST(UiPicture, ACopiedElementStartsWithNoPicture) {
        Harness h(kHudXml, kHudCss);
        h.frame();
        ASSERT_NE(h.document.root.picture.frame, nullptr);
        Element copy = h.document.root;
        EXPECT_EQ(copy.picture.frame, nullptr);
        EXPECT_FALSE(copy.picture.chrome.valid);
    }

    constexpr char kWorldXml[] = R"(
    <Canvas><Viewport class="world"><Stack class="column"><Stack class="unit"/></Stack></Viewport></Canvas>
)";

    class ShownVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<bool> shown{true};

        ShownVm() { property(engine::ui::intern("shown"), shown); }
    };

    bool drew_text(const ui_test::LogPainter &painter, std::string_view text) {
        for (const auto &call: painter.calls) {
            if (call.op == "text" && call.text == text) {
                return true;
            }
        }
        return false;
    }

    // A fixed box does not move when its only child leaves the flow, so nothing but that child is marked. The child's
    // mark has to rewrite the picture that already shows it.
    TEST(UiPicture, HidingWithIfDropsTheElementFromTheAncestorPicture) {
        ShownVm vm;
        auto parsed = engine::ui::parse_xml(R"(
            <Canvas>
              <Stack class="box"><Label class="gone" text="gone" if="{binding shown}"/></Stack>
            </Canvas>)",
                                            nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        UiDocument document = std::move(*parsed);
        const engine::ui::Stylesheet sheet = must_parse_css(
                ".box { width: 200px; height: 80px; background: #112233; repaint-boundary: yes; }"
                ".gone { font-size: 14px; width: 60px; height: 16px; color: #ffffff; }");
        ui_test::LogPainter painter;
        const engine::render::Rect canvas{0.0f, 0.0f, 400.0f, 300.0f};
        const auto frame = [&] {
            ASSERT_TRUE(engine::ui::apply_bindings(document, vm, nullptr, nullptr, &sheet).has_value());
            painter.calls.clear();
            engine::ui::paint_document(document, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = canvas,
                                                                .pointer = {-100.0f, -100.0f},
                                                                .delta_time = 0.016f,
                                                                .window_width = 400.0f,
                                                                .window_height = 300.0f});
        };
        frame();
        frame();
        ASSERT_TRUE(drew_text(painter, "gone"));
        Element *box = nullptr;
        for (Element &child: document.root.children) {
            if (std::ranges::find(child.classes, "box") != child.classes.end()) {
                box = &child;
            }
        }
        ASSERT_NE(box, nullptr);
        ASSERT_NE(box->picture.content.picture, nullptr);
        const Picture held = *box->picture.content.picture;

        vm.shown.set(false);
        frame();
        EXPECT_FALSE(drew_text(painter, "gone")) << "the hidden label is still in the ancestor's picture";
        EXPECT_FALSE(*box->picture.content.picture == held) << "the ancestor picture was not recorded again";

        const auto recorded = document.counters.pictures_recorded;
        frame();
        EXPECT_EQ(document.counters.pictures_recorded, recorded) << "the following frame only replays";
        EXPECT_FALSE(drew_text(painter, "gone"));

        vm.shown.set(true);
        frame();
        EXPECT_TRUE(drew_text(painter, "gone"));
    }

    // The hidden element is itself a boundary: the picture above holds a call of its frame, not its commands.
    TEST(UiPicture, HidingABoundaryWithIfDropsItsCallFromTheParentPicture) {
        ShownVm vm;
        auto parsed = engine::ui::parse_xml(R"(
            <Canvas>
              <Stack class="box" if="{binding shown}"><Label class="gone" text="gone"/></Stack>
            </Canvas>)",
                                            nullptr, &vm);
        ASSERT_TRUE(parsed.has_value());
        UiDocument document = std::move(*parsed);
        const engine::ui::Stylesheet sheet = must_parse_css(
                ".box { width: 200px; height: 80px; background: #112233; repaint-boundary: yes; }"
                ".gone { font-size: 14px; width: 60px; height: 16px; color: #ffffff; }");
        ui_test::LogPainter painter;
        const auto frame = [&] {
            ASSERT_TRUE(engine::ui::apply_bindings(document, vm, nullptr, nullptr, &sheet).has_value());
            painter.calls.clear();
            engine::ui::paint_document(document, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 400.0f, 300.0f},
                                                                .pointer = {-100.0f, -100.0f},
                                                                .delta_time = 0.016f,
                                                                .window_width = 400.0f,
                                                                .window_height = 300.0f});
        };
        frame();
        frame();
        ASSERT_TRUE(drew_text(painter, "gone"));
        ASSERT_NE(document.root.picture.content.picture, nullptr);
        const Picture held = *document.root.picture.content.picture;

        vm.shown.set(false);
        frame();
        EXPECT_FALSE(drew_text(painter, "gone"));
        EXPECT_FALSE(*document.root.picture.content.picture == held);

        const auto recorded = document.counters.pictures_recorded;
        frame();
        EXPECT_EQ(document.counters.pictures_recorded, recorded);
        EXPECT_FALSE(drew_text(painter, "gone"));
    }

    TEST(UiPicture, ACameraPanWithinTheRecordedWorldReplaysTheSamePicture) {
        auto parsed = engine::ui::parse_xml(kWorldXml);
        ASSERT_TRUE(parsed.has_value());
        UiDocument document = std::move(*parsed);
        Element *column = engine::ui::find_by_kind(document.root, engine::ui::ElementKind::Stack);
        ASSERT_NE(column, nullptr);
        Element unit = column->children.front();
        column->children.clear();
        for (int i = 0; i < 500; ++i) {
            column->children.push_back(unit);
        }
        document.reindex();
        const engine::ui::Stylesheet sheet =
                must_parse_css(".world { width: 200px; height: 200px; } .column { gap: 4px; } .unit { width: 40px; "
                               "height: 20px; background: #336699; }");
        ui_test::LogPainter painter;
        const auto paint = [&] {
            engine::ui::paint_document(document, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 400.0f, 400.0f},
                                                                .window_width = 400.0f,
                                                                .window_height = 400.0f});
        };
        paint();
        paint();
        Element *world = engine::ui::find_by_kind(document.root, engine::ui::ElementKind::Viewport);
        ASSERT_NE(world, nullptr);
        const auto recorded = document.counters.pictures_recorded;
        world->pan_y = -40.0f; // less than half a screen
        engine::ui::mark_dirty(document, *world, engine::ui::kDirtyFrame);
        paint();
        EXPECT_EQ(document.counters.pictures_recorded, recorded) << "within the margin: the same picture, a new camera";
        world->pan_y = -4000.0f;
        engine::ui::mark_dirty(document, *world, engine::ui::kDirtyFrame);
        paint();
        EXPECT_GT(document.counters.pictures_recorded, recorded) << "beyond it: recorded for the new part of the world";
    }

} // namespace
