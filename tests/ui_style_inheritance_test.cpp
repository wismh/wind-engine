#include <gtest/gtest.h>

#include "ui/dirty.h"
#include "ui/painter.h"
#include "ui_log_painter.h"

#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <glm/vec4.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// CSS inheritance: color, font, text alignment, white-space, visibility, user-select and selection-color pass from a
// parent's computed style to a child that no rule sets them on. docs/tech/modules/UI.md, Style.

namespace {

    using engine::ui::Element;
    using engine::ui::UiDocument;

    engine::ui::Stylesheet must_parse_css(std::string_view css) {
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(css, warnings);
        EXPECT_TRUE(sheet.has_value());
        EXPECT_TRUE(warnings.empty()) << (warnings.empty() ? "" : warnings[0]);
        return sheet.value_or(engine::ui::Stylesheet{});
    }

    class EmptyVm final : public engine::ui::ViewModel {};

    struct Harness {
        UiDocument document;
        engine::ui::Stylesheet sheet;
        EmptyVm vm;
        ui_test::LogPainter painter;
        engine::render::Rect canvas{0.0f, 0.0f, 600.0f, 400.0f};

        Harness(std::string_view xml, std::string_view css) : sheet(must_parse_css(css)) {
            auto parsed = engine::ui::parse_xml(xml);
            EXPECT_TRUE(parsed.has_value());
            document = std::move(*parsed);
        }

        void frame(glm::vec2 pointer = {-100.0f, -100.0f}) {
            ASSERT_TRUE(engine::ui::apply_bindings(document, vm, nullptr, nullptr, &sheet).has_value());
            painter.calls.clear();
            engine::ui::paint_document(document, &sheet, painter,
                                       engine::ui::UiPaintInput{.canvas_rect = canvas,
                                                                .pointer = pointer,
                                                                .window_width = 600.0f,
                                                                .window_height = 400.0f});
        }

        // The class names of `element` replaced, and the element marked for a restyle, the way a binding does.
        void set_classes(Element &element, std::vector<std::string> classes) {
            element.classes = std::move(classes);
            engine::ui::mark_dirty(document, element, engine::ui::kDirtyStyle | engine::ui::kDirtyPaint, true);
        }

        Element *find(std::string_view name) { return find_in(document.root, name); }

        // The colour the text `text` was drawn with on the last frame.
        glm::vec4 text_color(std::string_view text) const {
            for (const auto &call: painter.calls) {
                if (call.op == "text" && call.text == text) {
                    return call.color;
                }
            }
            return glm::vec4{-1.0f};
        }

    private:
        static Element *find_in(Element &element, std::string_view name) {
            if (std::ranges::find(element.classes, name) != element.classes.end()) {
                return &element;
            }
            for (Element &child: element.children) {
                if (Element *found = find_in(child, name)) {
                    return found;
                }
            }
            return nullptr;
        }
    };

    constexpr char kXml[] = R"(
    <Canvas>
      <Stack class="panel">
        <Stack class="mid"><Label class="t" text="hi"/><Label class="own" text="own"/></Stack>
      </Stack>
    </Canvas>
)";

    constexpr glm::vec4 kRed{1.0f, 0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 kGreen{0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 kBlue{0.0f, 0.0f, 1.0f, 1.0f};

    constexpr char kPanelCss[] = ".panel { width: 300; height: 200; pointer-events: all; } ";

    TEST(UiStyleInheritance, AChildTakesItsParentsTextProperties) {
        Harness h(kXml, std::string(kPanelCss) +
                                ".panel { color: #ff0000; font-size: 20px; text-align: center; white-space: nowrap; }");
        h.frame();
        const Element *label = h.find("t");
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(h.text_color("hi"), kRed);
        EXPECT_EQ(label->text_align, engine::ui::UiAlign::Center);
        EXPECT_EQ(label->white_space, engine::ui::WhiteSpace::NoWrap);
        EXPECT_EQ(label->font_size.unit, engine::ui::LengthUnit::Px);
        EXPECT_FLOAT_EQ(label->font_size.value, 20.0f);
    }

    TEST(UiStyleInheritance, ARuleOnTheChildWinsOverTheInheritedValue) {
        Harness h(kXml, std::string(kPanelCss) + ".panel { color: #ff0000; } .own { color: #0000ff; } "
                                                 "Label { font-size: 11px; }");
        h.frame();
        EXPECT_EQ(h.text_color("hi"), kRed);
        EXPECT_EQ(h.text_color("own"), kBlue);
        EXPECT_FLOAT_EQ(h.find("t")->font_size.value, 11.0f);
    }

    TEST(UiStyleInheritance, FontSizeInheritsTheComputedPixelsAndEmAndPercentFollowTheParent) {
        Harness h(kXml, std::string(kPanelCss) + ".panel { font-size: 20px; } .mid { font-size: 1.5em; } "
                                                 ".own { font-size: 50%; }");
        h.frame();
        EXPECT_FLOAT_EQ(h.find("mid")->font_size.value, 30.0f);
        EXPECT_FLOAT_EQ(h.find("t")->font_size.value, 30.0f) << "inherited from mid, already in px";
        EXPECT_FLOAT_EQ(h.find("own")->font_size.value, 15.0f) << "half of mid's computed size";
    }

    TEST(UiStyleInheritance, TheRootSeedsFromTheDefaults) {
        Harness h(kXml, kPanelCss);
        h.frame();
        EXPECT_FLOAT_EQ(h.find("t")->font_size.value, engine::ui::kDefaultFontSize);
        EXPECT_EQ(h.find("t")->text_align, engine::ui::UiAlign::Start);
    }

    TEST(UiStyleInheritance, NonInheritedPropertiesStayWithTheElement) {
        Harness h(kXml, std::string(kPanelCss) + ".panel { padding: 7; opacity: 0.5; width: 300; }");
        h.frame();
        EXPECT_FLOAT_EQ(h.find("mid")->padding.top.value, 0.0f);
        EXPECT_FLOAT_EQ(h.find("t")->padding.top.value, 0.0f);
    }

    TEST(UiStyleInheritance, AClassChangeOnTheParentRestylesTheDescendants) {
        Harness h(kXml, std::string(kPanelCss) + ".panel { color: #ff0000; } .alt { color: #0000ff; font-size: 30px; }");
        h.frame();
        h.frame();
        EXPECT_EQ(h.text_color("hi"), kRed);
        const float width_before = h.find("t")->layout_rect.w;

        h.set_classes(*h.find("panel"), {"panel", "alt"});
        h.frame();
        EXPECT_EQ(h.text_color("hi"), kBlue);
        EXPECT_FLOAT_EQ(h.find("t")->font_size.value, 30.0f);
        EXPECT_GT(h.find("t")->layout_rect.w, width_before) << "the bigger font laid the label out again";

        h.set_classes(*h.find("panel"), {"panel"});
        h.frame();
        EXPECT_EQ(h.text_color("hi"), kRed);
        EXPECT_FLOAT_EQ(h.find("t")->font_size.value, engine::ui::kDefaultFontSize);
    }

    TEST(UiStyleInheritance, HoveringTheParentRepaintsTheChildThatInheritsItsColor) {
        Harness h(kXml, std::string(kPanelCss) + ".panel { color: #ff0000; } .panel:hover { color: #00ff00; }");
        h.frame();
        h.frame();
        const Element *panel = h.find("panel");
        const engine::render::Rect rect = panel->layout_rect;
        const glm::vec2 inside{rect.x + rect.w - 5.0f, rect.y + rect.h - 5.0f};
        EXPECT_EQ(h.text_color("hi"), kRed);

        h.frame(inside);
        ASSERT_TRUE(panel->hovered);
        EXPECT_EQ(h.text_color("hi"), kGreen);

        h.frame({-100.0f, -100.0f});
        EXPECT_FALSE(panel->hovered);
        EXPECT_EQ(h.text_color("hi"), kRed);
    }

    TEST(UiStyleInheritance, AParentChangeThatInheritsNothingRestylesNoChild) {
        Harness h(kXml, std::string(kPanelCss) + ".panel:hover { background: #336699; }");
        h.frame();
        h.frame();
        const Element *panel = h.find("panel");
        const engine::render::Rect rect = panel->layout_rect;
        const glm::vec2 inside{rect.x + rect.w - 5.0f, rect.y + rect.h - 5.0f};
        const std::uint32_t mid = h.find("mid")->style_rebuilds;
        const std::uint32_t label = h.find("t")->style_rebuilds;

        h.frame(inside);
        ASSERT_TRUE(panel->hovered);
        h.frame({-100.0f, -100.0f});
        EXPECT_EQ(h.find("mid")->style_rebuilds, mid);
        EXPECT_EQ(h.find("t")->style_rebuilds, label);
    }

    TEST(UiStyleInheritance, ACursorStaysOutOfTheCascade) {
        Harness h(kXml, std::string(kPanelCss) + ".panel { cursor: pointer; }");
        h.frame();
        EXPECT_EQ(h.find("panel")->cursor, engine::ui::Cursor::Pointer);
        EXPECT_EQ(h.find("t")->cursor, engine::ui::Cursor::Auto) << "`auto` takes the ancestor's at hit time";
    }

}
