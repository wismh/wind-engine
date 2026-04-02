#include <gtest/gtest.h>

#include "ui/painter.h"
#include "ui/profile.h"

#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/profiler.h>
#include <engine/ui/view_model.h>

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

    engine::ecs::Entity spawn_named(engine::ecs::World &world, std::string_view id, bool with_label,
                                    engine::WindowId window = engine::kPrimaryWindow) {
        const std::string xml = with_label ? std::format(R"(<Canvas id="{}"><Label>Hi</Label></Canvas>)", id)
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

#else

TEST(UiProfiler, ReleaseApiIsANoOp) {
    engine::ecs::World world;
    engine::ui::set_ui_profiler_attached(world, true);
    EXPECT_FALSE(engine::ui::kUiProfilerBuilt);
    EXPECT_FALSE(engine::ui::ui_profiler_attached(world));
    EXPECT_TRUE(engine::ui::profiler_canvases(world).empty());
    EXPECT_TRUE(engine::ui::profiler_shared_frames(world).empty());
}

#endif
