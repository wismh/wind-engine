#include <gtest/gtest.h>

#include "ui/painter.h"
#include "ui/style_anim.h"
#include "ui_log_painter.h"

#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <cstdint>
#include <string>
#include <string_view>

// wind-229: @keyframes compile to numeric tracks once, and a motion sample marks layout only where a layout field
// moved.

namespace {

    using engine::ui::Element;
    using engine::ui::MotionProp;
    using engine::ui::UiDocument;

    engine::ui::Stylesheet must_parse_css(std::string_view css) {
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(css, warnings);
        EXPECT_TRUE(sheet.has_value());
        return *sheet;
    }

    struct Harness {
        UiDocument document;
        engine::ui::Stylesheet sheet;
        ui_test::LogPainter painter;

        Harness(std::string_view xml, std::string_view css) : sheet(must_parse_css(css)) {
            auto parsed = engine::ui::parse_xml(xml);
            EXPECT_TRUE(parsed.has_value());
            document = std::move(*parsed);
        }

        void frame(float dt) {
            painter.calls.clear();
            engine::ui::paint_document(document, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 600.0f, 400.0f},
                                                                .pointer = {-100.0f, -100.0f},
                                                                .delta_time = dt,
                                                                .window_width = 600.0f,
                                                                .window_height = 400.0f});
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
            return nullptr;
        }
    };

    // The opacity an element shows: the motion sample when it holds one, else 1.
    float shown_opacity(const Element &element) {
        for (const engine::ui::ShownMotion &shown: element.motion_shown) {
            if (shown.prop == MotionProp::Opacity) {
                return shown.value.number;
            }
        }
        return 1.0f;
    }

    std::uint64_t layout_runs_of(const Element &element) {
        std::uint64_t total = element.layout_runs;
        for (const Element &child: element.children) {
            total += layout_runs_of(child);
        }
        return total;
    }

    constexpr char kXml[] = R"(
    <Canvas>
      <Stack class="page">
        <Stack class="box"><Label class="inside" text="inside"/></Stack>
        <Stack class="fixed"><Stack class="grower"/></Stack>
        <Stack class="other"><Label class="note" text="note"/></Stack>
      </Stack>
    </Canvas>
)";

    TEST(UiAnimationTracks, ATrackSampleEqualsTheValueSampledFromTheDeclarations) {
        const engine::ui::Stylesheet sheet = must_parse_css(R"(
        @keyframes mixed {
            0% { opacity: 0; width: 10px; padding: 2px; background: #102030; }
            40% { opacity: 0.5; width: 50%; padding: 6px; background: #405060; }
            100% { opacity: 1; width: 90px; padding: 10px; background: #ffffff; }
        }
    )");
        const engine::ui::Keyframes &frames = sheet.keyframes.front();
        const glm::vec2 basis{300.0f, 200.0f};
        for (const MotionProp prop:
             {MotionProp::Opacity, MotionProp::Width, MotionProp::Padding, MotionProp::Background}) {
            for (float t = 0.0f; t <= 1.0001f; t += 0.0625f) {
                const auto fast = engine::ui::sample_keyframes(frames, prop, t, basis);
                const auto slow = engine::ui::sample_keyframes_from_declarations(frames, prop, t, basis);
                ASSERT_EQ(fast.has_value(), slow.has_value());
                if (!fast) {
                    continue;
                }
                EXPECT_FLOAT_EQ(fast->number, slow->number);
                EXPECT_FLOAT_EQ(fast->px, slow->px);
                EXPECT_FLOAT_EQ(fast->px_r, slow->px_r);
                EXPECT_FLOAT_EQ(fast->color.r, slow->color.r);
                EXPECT_FLOAT_EQ(fast->color.b, slow->color.b);
            }
        }
        EXPECT_FALSE(engine::ui::sample_keyframes(frames, MotionProp::Height, 0.5f, basis).has_value());
    }

    TEST(UiAnimationTracks, TheTracksAreBuiltOnceAndRebuiltWhenTheStopsChange) {
        engine::ui::Stylesheet sheet = must_parse_css("@keyframes k { from { opacity: 0; } to { opacity: 1; } }");
        engine::ui::Keyframes &frames = sheet.keyframes.front();
        ASSERT_EQ(frames.compiled, nullptr);
        ASSERT_TRUE(engine::ui::sample_keyframes(frames, MotionProp::Opacity, 0.5f, {}).has_value());
        const auto built = frames.compiled;
        ASSERT_NE(built, nullptr);
        engine::ui::sample_keyframes(frames, MotionProp::Opacity, 0.7f, {});
        EXPECT_EQ(frames.compiled, built) << "a second sample reuses the tracks";
        frames.stops.push_back(engine::ui::KeyframeStop{0.5f, {engine::ui::CssDeclaration{"opacity", "0.2"}}});
        EXPECT_NEAR(engine::ui::sample_keyframes(frames, MotionProp::Opacity, 0.5f, {})->number, 0.2f, 1e-5f);
        EXPECT_NE(frames.compiled, built);
    }

    TEST(UiAnimationTracks, ASteadyOpacityAnimationLaysOutNothing) {
        Harness h(kXml, R"(
        .page { gap: 4; }
        .box { animation: pulse 1s linear infinite; }
        @keyframes pulse { from { opacity: 0.2; } to { opacity: 1; } }
    )");
        h.frame(0.016f);
        h.frame(0.016f);
        const std::uint64_t runs = layout_runs_of(h.document.root);
        const std::uint64_t passes = h.document.counters.layout_passes;
        for (int i = 0; i < 30; ++i) {
            h.frame(0.016f);
        }
        EXPECT_EQ(layout_runs_of(h.document.root), runs);
        EXPECT_EQ(h.document.counters.layout_passes, passes);
        EXPECT_LT(shown_opacity(*h.find("box")), 1.0f);
    }

    TEST(UiAnimationTracks, AWidthAnimationLaysOutOnlyTheChainThatChangedSize) {
        Harness h(kXml, R"(
        .page { gap: 4; }
        .box { width: 40px; animation: grow 1s linear infinite; }
        .fixed { width: 100px; height: 40px; }
        .grower { height: 10px; }
        @keyframes grow { from { width: 40px; } to { width: 200px; } }
    )");
        h.frame(0.016f);
        h.frame(0.016f);
        const Element *other = h.find("other");
        const Element *note = h.find("note");
        const Element *fixed = h.find("fixed");
        const std::uint32_t other_runs = other->layout_runs;
        const std::uint32_t note_runs = note->layout_runs;
        const std::uint32_t fixed_runs = fixed->layout_runs;
        const std::uint32_t box_runs = h.find("box")->layout_runs;
        const float width = h.find("box")->layout_rect.w;
        for (int i = 0; i < 5; ++i) {
            h.frame(0.1f);
        }
        EXPECT_GT(h.find("box")->layout_rect.w, width) << "the box grew";
        EXPECT_GT(h.find("box")->layout_runs, box_runs);
        EXPECT_EQ(other->layout_runs, other_runs) << "a sibling that did not change size is not laid out";
        EXPECT_EQ(note->layout_runs, note_runs);
        EXPECT_EQ(fixed->layout_runs, fixed_runs);
    }

    TEST(UiAnimationTracks, AFinishedAnimationKeepsItsEndValueAndStopsWork) {
        Harness h(kXml, R"(
        .box { animation: fade 0.2s linear 1; }
        @keyframes fade { from { opacity: 1; } to { opacity: 0.25; } }
    )");
        for (int i = 0; i < 6; ++i) {
            h.frame(0.1f);
        }
        const Element *box = h.find("box");
        EXPECT_NEAR(shown_opacity(*box), 0.25f, 1e-4f);
        const std::uint32_t styles = box->style_rebuilds;
        const std::uint64_t epoch = h.document.epoch;
        for (int i = 0; i < 5; ++i) {
            h.frame(0.1f);
        }
        EXPECT_NEAR(shown_opacity(*box), 0.25f, 1e-4f);
        EXPECT_EQ(box->style_rebuilds, styles) << "the finished track costs no cascade";
        EXPECT_EQ(h.document.epoch, epoch) << "no motion keeps the hover target search alive";
    }

} // namespace
