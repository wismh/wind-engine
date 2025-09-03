#include <gtest/gtest.h>

#include "ui/painter.h"
#include "ui/profile.h"

#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/profiler.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <array>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(NANOVG_H) || defined(NANOVG_GL_H) || defined(NANOVG_GL3)
#error "ui profiler tests must not include nvg headers"
#endif

namespace {

    class EmptyModel final : public engine::ui::ViewModel {};

    class NullPainter final : public engine::ui::IUiPainter {
    public:
        void save() override {}
        void restore() override {}
        void scissor(const engine::render::Rect &) override {}
        void apply_transform(glm::vec2, float, float) override {}
        void apply_view(glm::vec2, glm::vec2, float) override {}
        void set_opacity(float) override {}
        void fill_rounded_rect(const engine::render::Rect &, float, glm::vec4) override {}
        void fill_rounded_rect_gradient(const engine::render::Rect &, float, const engine::ui::Gradient &) override {}
        void stroke_rounded_rect(const engine::render::Rect &, float, float, glm::vec4) override {}
        void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
        void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
        void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override {}
        void set_font(engine::AssetId, float) override {}
        void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {}
        void image(engine::AssetId, const engine::render::Rect &) override {}
        void image_repeat(engine::AssetId, const engine::render::Rect &) override {}
        void image_nine_slice(engine::AssetId, const engine::render::Rect &, const engine::ui::BoxInsets &) override {}
        glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
            return {static_cast<float>(text.size()) * size * 0.5f, size};
        }
    };

    // Counts what it receives by profiler kind, and queues one draw call per drawing call, like a batching painter.
    class RecordingPainter final : public engine::ui::IUiPainter {
    public:
        using Kind = engine::ui::ProfilerPaintKind;

        std::array<int, engine::ui::kProfilerPaintKindCount> counts{};
        int queued = 0;

        [[nodiscard]] int count(Kind kind) const { return counts[static_cast<std::size_t>(kind)]; }

        void save() override { add(Kind::Save, false); }
        void restore() override { add(Kind::Restore, false); }
        void scissor(const engine::render::Rect &) override { add(Kind::Scissor, false); }
        void apply_transform(glm::vec2, float, float) override { add(Kind::Transform, false); }
        void apply_view(glm::vec2, glm::vec2, float) override { add(Kind::View, false); }
        void set_opacity(float) override { add(Kind::Opacity, false); }
        void fill_rounded_rect(const engine::render::Rect &, float radius, glm::vec4) override {
            add(radius > 0.0f ? Kind::FillRoundedRect : Kind::FillRect, true);
        }
        void fill_rounded_rect_gradient(const engine::render::Rect &, float,
                                        const engine::ui::Gradient &gradient) override {
            switch (gradient.kind) {
                case engine::ui::GradientKind::Linear:
                    add(Kind::LinearGradient, true);
                    break;
                case engine::ui::GradientKind::Radial:
                    add(Kind::RadialGradient, true);
                    break;
                case engine::ui::GradientKind::Conic:
                    add(Kind::ConicGradient, true);
                    break;
            }
        }
        void stroke_rounded_rect(const engine::render::Rect &, float, float, glm::vec4) override {
            add(Kind::StrokeRect, true);
        }
        void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override { add(Kind::Line, true); }
        void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override { add(Kind::Arc, true); }
        void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override { add(Kind::Path, true); }
        void set_font(engine::AssetId, float) override { add(Kind::Font, false); }
        void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {
            add(Kind::Text, true);
        }
        void image(engine::AssetId, const engine::render::Rect &) override { add(Kind::Image, true); }
        void image_repeat(engine::AssetId, const engine::render::Rect &) override { add(Kind::ImageRepeat, true); }
        void image_nine_slice(engine::AssetId, const engine::render::Rect &, const engine::ui::BoxInsets &) override {
            add(Kind::NineSlice, true);
        }
        glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
            return {static_cast<float>(text.size()) * size * 0.5f, size};
        }
        int queued_draw_calls() override { return queued; }

    private:
        void add(Kind kind, bool draws) {
            ++counts[static_cast<std::size_t>(kind)];
            if (draws) {
                ++queued;
            }
        }
    };

    // Solid, rounded, linear and radial gradient fills, a border, and a text run.
    engine::ecs::Entity spawn_styled(engine::ecs::World &world) {
        const auto parsed = engine::ui::parse_xml(R"(<Canvas id="styled"><Stack id="solid"/><Stack id="round"/>)"
                                                  R"(<Stack id="ramp"/><Stack id="ring"/><Stack id="edge"/>)"
                                                  R"(<Label text="Hi"/></Canvas>)");
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css("Stack { width: 20px; height: 10px; }\n"
                                           "#solid { background: #ff0000; }\n"
                                           "#round { background: #00ff00; border-radius: 4px; }\n"
                                           "#ramp { background: linear-gradient(90deg, #000000, #ffffff); }\n"
                                           "#ring { background: radial-gradient(#000000, #ffffff); }\n"
                                           "#edge { border-width: 1px; border-color: #ffffff; }\n",
                                           warnings);
        if (!parsed.has_value() || !sheet.has_value()) {
            ADD_FAILURE() << "styled canvas did not parse";
            return {};
        }
        EXPECT_TRUE(warnings.empty());
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.rect = {0.0f, 0.0f, 120.0f, 80.0f};
        canvas.data_context = std::make_shared<EmptyModel>();
        return engine::ui::spawn_canvas(world, std::move(canvas), *parsed, std::move(*sheet));
    }

    void paint_styled(engine::ecs::World &world, engine::ecs::Entity entity, engine::ui::IUiPainter &painter,
                      engine::ecs::Entity timed) {
        engine::ui::UiInstance &instance = world.get<engine::ui::UiInstance>(entity);
        engine::ui::paint_document(instance.document, instance.stylesheet ? &*instance.stylesheet : nullptr, painter,
                                   engine::ui::UiPaintInput{
                                           .canvas_rect = {0.0f, 0.0f, 120.0f, 80.0f},
                                           .window_width = 120.0f,
                                           .window_height = 80.0f,
                                           .canvas = timed,
                                   });
    }

    engine::ecs::Entity spawn_named(engine::ecs::World &world, std::string_view id, bool with_label,
                                    engine::WindowId window = engine::kPrimaryWindow) {
        const std::string xml = with_label ? std::format(R"(<Canvas id="{}"><Label text="Hi"/></Canvas>)", id)
                                           : std::format(R"(<Canvas id="{}"/>)", id);
        const auto parsed = engine::ui::parse_xml(xml);
        if (!parsed.has_value()) {
            ADD_FAILURE() << "canvas xml did not parse";
            return {};
        }
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.rect = {0.0f, 0.0f, 120.0f, 80.0f};
        canvas.window = window;
        canvas.data_context = std::make_shared<EmptyModel>();
        return engine::ui::spawn_canvas(world, std::move(canvas), *parsed);
    }

    // What the backend does for a CmdDrawUI: `canvas` is the entity only while that world is profiled.
    void paint_canvas(engine::ecs::World &world, engine::ecs::Entity entity, NullPainter &painter,
                      engine::ecs::Entity timed) {
        engine::ui::UiInstance &instance = world.get<engine::ui::UiInstance>(entity);
        engine::ui::paint_document(instance.document, nullptr, painter,
                                   engine::ui::UiPaintInput{
                                           .canvas_rect = {0.0f, 0.0f, 120.0f, 80.0f},
                                           .window_width = 120.0f,
                                           .window_height = 80.0f,
                                           .canvas = timed,
                                   });
    }

    void paint_canvas(engine::ecs::World &world, engine::ecs::Entity entity, NullPainter &painter) {
        paint_canvas(world, entity, painter, entity);
    }

    // Detaches on scope exit so no test leaves the scopes pointing at a destroyed world.
    struct Attached {
        engine::ecs::World &world;

        explicit Attached(engine::ecs::World &w) : world(w) { engine::ui::set_ui_profiler_attached(world, true); }
        ~Attached() { engine::ui::set_ui_profiler_attached(world, false); }
        Attached(const Attached &) = delete;
        Attached &operator=(const Attached &) = delete;
    };

} // namespace

#if defined(ENGINE_UI_PROFILER)

TEST(UiProfiler, DetachedWorldStoresNothing) {
    engine::ecs::World world;
    const engine::ecs::Entity hud = spawn_named(world, "hud", true);
    NullPainter painter;
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    EXPECT_FALSE(engine::ui::ui_profiler_attached(world));
    EXPECT_TRUE(engine::ui::profiler_frames(world, hud).empty());
    EXPECT_TRUE(engine::ui::profiler_shared_frames(world).empty());
}

TEST(UiProfiler, BeginFrameIsStoredOnlyWhileAttached) {
    engine::ecs::World world;
    engine::ui::set_ui_profiler_attached(world, true);
    EXPECT_TRUE(engine::ui::ui_profiler_attached(world));
    engine::ui::begin_frame(world);
    engine::ui::begin_frame(world);
    EXPECT_GE(engine::ui::profiler_shared_frames(world).size(), 1u);
    engine::ui::set_ui_profiler_attached(world, false);
    EXPECT_TRUE(engine::ui::profiler_shared_frames(world).empty());
    EXPECT_FALSE(engine::ui::ui_profiler_attached(world));
}

TEST(UiProfiler, BindSamplesStayOnTheirCanvas) {
    engine::ecs::World world;
    const engine::ecs::Entity alpha = spawn_named(world, "alpha", false);
    const engine::ecs::Entity beta = spawn_named(world, "beta", false);
    Attached attached{world};

    engine::register_engine_systems(world);
    world.run(engine::ecs::Schedule::Frame);
    engine::ui::begin_frame(world);

    const std::vector<engine::ui::ProfilerFrame> alpha_frames = engine::ui::profiler_frames(world, alpha);
    const std::vector<engine::ui::ProfilerFrame> beta_frames = engine::ui::profiler_frames(world, beta);
    ASSERT_FALSE(alpha_frames.empty());
    EXPECT_TRUE(alpha_frames.back().saw_bindings);
    ASSERT_FALSE(beta_frames.empty());
    EXPECT_TRUE(beta_frames.back().saw_bindings);
}

TEST(UiProfiler, AnotherWorldsPassesStayOutOfTheProfiledWorld) {
    engine::ecs::World game;
    engine::ecs::World editor;
    const engine::ecs::Entity hud = spawn_named(game, "hud", true);
    const engine::ecs::Entity panel = spawn_named(editor, "panel", true);
    ASSERT_EQ(hud, panel) << "the same entity in two worlds is what the scopes must not mix up";
    Attached attached{game};
    engine::register_engine_systems(editor);
    NullPainter painter;

    engine::ui::begin_frame(game);
    editor.run(engine::ecs::Schedule::Frame);
    paint_canvas(editor, panel, painter, engine::ecs::Entity{});
    engine::ui::begin_frame(game);
    EXPECT_TRUE(engine::ui::profiler_frames(game, hud).empty()) << "editor bind and paint went to the game";
    EXPECT_FALSE(engine::ui::ui_profiler_attached(editor));
    EXPECT_TRUE(engine::ui::profiler_frames(editor, panel).empty());

    paint_canvas(game, hud, painter);
    engine::ui::begin_frame(game);
    const std::vector<engine::ui::ProfilerFrame> frames = engine::ui::profiler_frames(game, hud);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_TRUE(frames.back().saw_paint);
    EXPECT_FALSE(frames.back().saw_bindings);
}

TEST(UiProfiler, PaintRecordsLayoutThenASkip) {
    engine::ecs::World world;
    const engine::ecs::Entity hud = spawn_named(world, "hud", true);
    Attached attached{world};
    NullPainter painter;
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    std::vector<engine::ui::ProfilerFrame> frames = engine::ui::profiler_frames(world, hud);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_TRUE(frames[0].saw_paint);
    EXPECT_TRUE(frames[0].layout_ran);
    EXPECT_FALSE(frames[0].layout_skipped());
    EXPECT_GE(frames[0].elements, 2);
    EXPECT_EQ(frames[0].generated, 0);

    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    frames = engine::ui::profiler_frames(world, hud);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_TRUE(frames[1].saw_paint);
    EXPECT_FALSE(frames[1].layout_ran);
    EXPECT_TRUE(frames[1].layout_skipped());
    EXPECT_GE(frames[1].elements, 2);
}

TEST(UiProfiler, PauseKeepsTheRings) {
    engine::ecs::World world;
    const engine::ecs::Entity hud = spawn_named(world, "hud", true);
    Attached attached{world};
    NullPainter painter;
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    ASSERT_EQ(engine::ui::profiler_frames(world, hud).size(), 1u);

    engine::ui::set_profiler_paused(world, true);
    EXPECT_TRUE(engine::ui::profiler_paused(world));
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    const std::vector<engine::ui::ProfilerFrame> held = engine::ui::profiler_frames(world, hud);
    ASSERT_EQ(held.size(), 1u);
    EXPECT_TRUE(held[0].layout_ran);

    engine::ui::set_profiler_paused(world, false);
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    EXPECT_EQ(engine::ui::profiler_frames(world, hud).size(), 2u);

    engine::ui::set_profiler_paused(world, true);
    engine::ui::set_ui_profiler_attached(world, false);
    EXPECT_FALSE(engine::ui::profiler_paused(world)) << "detaching clears Pause";
}

TEST(UiProfiler, RingKeepsTheLastFramesOldestFirst) {
    engine::ecs::World world;
    Attached attached{world};
    for (int i = 0; i < engine::ui::kProfilerRingFrames + 10; ++i) {
        engine::ui::begin_frame(world);
    }
    const std::vector<engine::ui::ProfilerSharedFrame> frames = engine::ui::profiler_shared_frames(world);
    EXPECT_EQ(frames.size(), static_cast<std::size_t>(engine::ui::kProfilerRingFrames));
}

TEST(UiProfiler, ClearEmptiesTheRingsAndKeepsTheOpenFrame) {
    engine::ecs::World world;
    const engine::ecs::Entity hud = spawn_named(world, "hud", true);
    Attached attached{world};
    NullPainter painter;
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    engine::ui::begin_frame(world);
    ASSERT_EQ(engine::ui::profiler_frames(world, hud).size(), 1u);
    ASSERT_FALSE(engine::ui::profiler_shared_frames(world).empty());

    // A tick in flight: its paint is open when the rings are cleared, and is the first frame stored after.
    paint_canvas(world, hud, painter);
    engine::ui::profiler_clear(world);
    EXPECT_TRUE(engine::ui::profiler_frames(world, hud).empty());
    EXPECT_TRUE(engine::ui::profiler_shared_frames(world).empty());
    EXPECT_TRUE(engine::ui::ui_profiler_attached(world));

    engine::ui::begin_frame(world);
    const std::vector<engine::ui::ProfilerFrame> frames = engine::ui::profiler_frames(world, hud);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_TRUE(frames[0].saw_paint);
    EXPECT_GE(frames[0].elements, 2);
    EXPECT_EQ(engine::ui::profiler_canvases(world).front().frames, 1);
}

TEST(UiProfiler, CanvasListSelectsTheFirstAndFollowsSelect) {
    engine::ecs::World world;
    const engine::ecs::Entity alpha = spawn_named(world, "alpha", false);
    const engine::ecs::Entity beta = spawn_named(world, "", false);
    Attached attached{world};

    std::vector<engine::ui::ProfilerCanvas> canvases = engine::ui::profiler_canvases(world);
    ASSERT_EQ(canvases.size(), 2u);
    EXPECT_EQ(canvases[0].label, "alpha");
    EXPECT_EQ(canvases[1].label, "Canvas");
    EXPECT_TRUE(canvases[0].selected);
    EXPECT_EQ(engine::ui::profiler_selected(world), alpha);

    engine::ui::profiler_select(world, beta);
    canvases = engine::ui::profiler_canvases(world);
    EXPECT_FALSE(canvases[0].selected);
    EXPECT_TRUE(canvases[1].selected);

    world.destroy(beta);
    canvases = engine::ui::profiler_canvases(world);
    ASSERT_EQ(canvases.size(), 1u);
    EXPECT_TRUE(canvases[0].selected) << "a dead selection falls back to the first canvas";
    EXPECT_EQ(engine::ui::profiler_selected(world), alpha);
}

TEST(UiProfiler, CanvasListPrefixesWindowsWhenThereAreSeveral) {
    engine::ecs::World world;
    (void) spawn_named(world, "hud", false);
    (void) spawn_named(world, "tool", false, engine::WindowId{4});
    Attached attached{world};
    const std::vector<engine::ui::ProfilerCanvas> canvases = engine::ui::profiler_canvases(world);
    ASSERT_EQ(canvases.size(), 2u);
    EXPECT_EQ(canvases[0].label, "[0] hud");
    EXPECT_EQ(canvases[1].label, "[4] tool");
    EXPECT_EQ(canvases[1].window, engine::WindowId{4});
}

TEST(UiProfiler, DetachDropsTheRingsUnlessCaptureIsOn) {
    engine::ecs::World world;
    const engine::ecs::Entity hud = spawn_named(world, "hud", true);
    NullPainter painter;
    engine::ui::set_ui_profiler_attached(world, true);
    engine::ui::profiler_cli_set_capture(world, true);
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    engine::ui::set_ui_profiler_attached(world, false);
    EXPECT_EQ(engine::ui::profiler_frames(world, hud).size(), 1u) << "capture keeps the rings";
    EXPECT_TRUE(engine::ui::profiler_cli_ready(world));

    engine::ui::profiler_cli_set_capture(world, false);
    EXPECT_TRUE(engine::ui::profiler_frames(world, hud).empty());
}

TEST(UiProfiler, PaintCountsPainterCallsByKind) {
    engine::ecs::World world;
    const engine::ecs::Entity styled = spawn_styled(world);
    Attached attached{world};
    RecordingPainter painter;
    paint_styled(world, styled, painter, styled);
    engine::ui::begin_frame(world);

    const std::vector<engine::ui::ProfilerFrame> frames = engine::ui::profiler_frames(world, styled);
    ASSERT_EQ(frames.size(), 1u);
    const engine::ui::ProfilerFrame &frame = frames[0];
    EXPECT_EQ(frame.paint_commands, painter.counts) << "every call the painter received is counted once";
    using Kind = engine::ui::ProfilerPaintKind;
    EXPECT_GE(frame.paint(Kind::FillRect), 1);
    EXPECT_GE(frame.paint(Kind::FillRoundedRect), 1);
    EXPECT_GE(frame.paint(Kind::LinearGradient), 1);
    EXPECT_GE(frame.paint(Kind::RadialGradient), 1);
    EXPECT_EQ(frame.paint(Kind::ConicGradient), 0);
    EXPECT_GE(frame.paint(Kind::StrokeRect), 1);
    EXPECT_GE(frame.paint(Kind::Text), 1);
    EXPECT_GE(frame.paint(Kind::Scissor), 1);
    EXPECT_GE(frame.paint(Kind::Save), 1);
    EXPECT_EQ(frame.paint(Kind::Save), frame.paint(Kind::Restore));
    EXPECT_EQ(frame.draw_calls, painter.queued);
    EXPECT_GT(frame.draw_calls, 0);
}

TEST(UiProfiler, PaintIsCountedOnlyWhileRecorded) {
    engine::ecs::World world;
    const engine::ecs::Entity styled = spawn_styled(world);
    RecordingPainter painter;
    paint_styled(world, styled, painter, styled);
    engine::ui::begin_frame(world);
    EXPECT_GT(painter.count(engine::ui::ProfilerPaintKind::Text), 0) << "an unrecorded paint still draws";
    EXPECT_TRUE(engine::ui::profiler_frames(world, styled).empty()) << "detached";

    Attached attached{world};
    RecordingPainter unprofiled;
    // run_ui_render leaves CmdDrawUI::canvas empty for a world that is not the recorded one.
    paint_styled(world, styled, unprofiled, engine::ecs::Entity{});
    engine::ui::begin_frame(world);
    EXPECT_GT(unprofiled.count(engine::ui::ProfilerPaintKind::Text), 0);
    EXPECT_TRUE(engine::ui::profiler_frames(world, styled).empty()) << "an empty canvas field records nothing";
}

TEST(UiProfiler, DrawCallsAreTheQueueGrowthDuringEachCanvas) {
    engine::ecs::World world;
    const engine::ecs::Entity first = spawn_styled(world);
    const engine::ecs::Entity second = spawn_named(world, "hud", true);
    Attached attached{world};
    // One painter per window, shared by its canvases; its queue is flushed once after all of them.
    RecordingPainter painter;
    painter.queued = 7;
    paint_styled(world, first, painter, first);
    const int after_first = painter.queued;
    paint_styled(world, second, painter, second);
    engine::ui::begin_frame(world);

    const std::vector<engine::ui::ProfilerFrame> first_frames = engine::ui::profiler_frames(world, first);
    const std::vector<engine::ui::ProfilerFrame> second_frames = engine::ui::profiler_frames(world, second);
    ASSERT_EQ(first_frames.size(), 1u);
    ASSERT_EQ(second_frames.size(), 1u);
    EXPECT_EQ(first_frames[0].draw_calls, after_first - 7);
    EXPECT_EQ(second_frames[0].draw_calls, painter.queued - after_first);
    EXPECT_GT(second_frames[0].draw_calls, 0);
}

TEST(UiProfiler, PaintCountsStartOverEachFrame) {
    engine::ecs::World world;
    const engine::ecs::Entity styled = spawn_styled(world);
    Attached attached{world};
    RecordingPainter painter;
    paint_styled(world, styled, painter, styled);
    engine::ui::begin_frame(world);
    const std::array<int, engine::ui::kProfilerPaintKindCount> one_frame = painter.counts;
    const int one_frame_draws = painter.queued;
    paint_styled(world, styled, painter, styled);
    engine::ui::begin_frame(world);

    std::vector<engine::ui::ProfilerFrame> frames = engine::ui::profiler_frames(world, styled);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].paint_commands, one_frame);
    EXPECT_EQ(frames[1].paint_commands, one_frame) << "the same document paints the same calls";
    EXPECT_EQ(frames[1].draw_calls, one_frame_draws);

    // Two paint passes of one canvas in a frame (base and popup layer) add up.
    paint_styled(world, styled, painter, styled);
    paint_styled(world, styled, painter, styled);
    engine::ui::begin_frame(world);
    frames = engine::ui::profiler_frames(world, styled);
    ASSERT_EQ(frames.size(), 3u);
    const engine::ui::ProfilerPaintKind text = engine::ui::ProfilerPaintKind::Text;
    EXPECT_EQ(frames[2].paint(text), 2 * frames[0].paint(text));
    EXPECT_EQ(frames[2].draw_calls, 2 * one_frame_draws);

    for (int i = 0; i < engine::ui::kProfilerRingFrames; ++i) {
        paint_styled(world, styled, painter, styled);
        engine::ui::begin_frame(world);
    }
    frames = engine::ui::profiler_frames(world, styled);
    ASSERT_EQ(frames.size(), static_cast<std::size_t>(engine::ui::kProfilerRingFrames));
    EXPECT_EQ(frames.front().paint_commands, one_frame) << "the doubled frame left the ring";
}

TEST(UiProfiler, CliJsonHasDrawCallsAndPaintCommands) {
    engine::ecs::World world;
    const engine::ecs::Entity styled = spawn_styled(world);
    Attached attached{world};
    RecordingPainter painter;
    paint_styled(world, styled, painter, styled);
    engine::ui::begin_frame(world);
    const std::string json = engine::ui::profiler_json(world);
    const int draws = painter.queued;
    EXPECT_NE(json.find(std::format(R"("draw_calls":{{"last":{},"avg":{}.00,"max":{}}})", draws, draws, draws)),
              std::string::npos)
            << json;
    const int texts = painter.count(engine::ui::ProfilerPaintKind::Text);
    EXPECT_NE(json.find(std::format(R"("text":{{"last":{},"avg":{}.00,"max":{}}})", texts, texts, texts)),
              std::string::npos)
            << json;
    EXPECT_NE(json.find(R"("conic_gradient":{"last":0,"avg":0.00,"max":0})"), std::string::npos) << json;
}

#else

// Without ENGINE_UI_PROFILER: an exported game's Release and MinSizeRel. The editor build always has it.
TEST(UiProfiler, CompiledOutApiIsANoOp) {
    engine::ecs::World world;
    engine::ui::set_ui_profiler_attached(world, true);
    EXPECT_FALSE(engine::ui::kUiProfilerBuilt);
    EXPECT_FALSE(engine::ui::ui_profiler_attached(world));
    EXPECT_TRUE(engine::ui::profiler_canvases(world).empty());
    EXPECT_TRUE(engine::ui::profiler_shared_frames(world).empty());
    engine::ui::profiler_clear(world);
    EXPECT_TRUE(engine::ui::profiler_json(world).empty());
    // The painted document still draws; nothing counts its calls.
    const engine::ecs::Entity styled = spawn_styled(world);
    RecordingPainter painter;
    paint_styled(world, styled, painter, styled);
    EXPECT_GT(painter.count(engine::ui::ProfilerPaintKind::Text), 0);
    EXPECT_TRUE(engine::ui::profiler_frames(world, styled).empty());
    EXPECT_EQ(engine::ui::profiler_paint_kind_name(engine::ui::ProfilerPaintKind::NineSlice), "nine_slice");
}

#endif
