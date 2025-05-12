#include <gtest/gtest.h>

#include "ui/painter.h"
#include "ui/profile.h"

#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/profiler.h>
#include <engine/ui/view_model.h>

#include <format>
#include <memory>
#include <string>
#include <string_view>

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

    engine::ui::Element *find_id(engine::ui::Element &element, std::string_view id) {
        if (element.id == id) {
            return &element;
        }
        for (engine::ui::Element &child: element.children) {
            if (engine::ui::Element *found = find_id(child, id)) {
                return found;
            }
        }
        for (engine::ui::Element &child: element.generated_items) {
            if (engine::ui::Element *found = find_id(child, id)) {
                return found;
            }
        }
        return nullptr;
    }

    engine::ui::Element *find_text(engine::ui::Element &element, std::string_view needle) {
        if (element.text.find(needle) != std::string::npos) {
            return &element;
        }
        for (engine::ui::Element &child: element.children) {
            if (engine::ui::Element *found = find_text(child, needle)) {
                return found;
            }
        }
        for (engine::ui::Element &child: element.generated_items) {
            if (engine::ui::Element *found = find_text(child, needle)) {
                return found;
            }
        }
        return nullptr;
    }

    engine::ecs::Entity spawn_named(engine::ecs::World &world, std::string_view id, bool with_label) {
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
        canvas.data_context = std::make_shared<EmptyModel>();
        return engine::ui::spawn_canvas(world, std::move(canvas), *parsed);
    }

    void layout_instance(engine::ecs::World &world, engine::ecs::Entity entity) {
        engine::ui::UiInstance &instance = world.get<engine::ui::UiInstance>(entity);
        engine::ui::UiCanvas &canvas = world.get<engine::ui::UiCanvas>(entity);
        if (canvas.data_context) {
            ASSERT_TRUE(engine::ui::apply_bindings(instance.document, *canvas.data_context).has_value());
        }
        const engine::ui::Stylesheet *sheet = instance.stylesheet ? &*instance.stylesheet : nullptr;
        const engine::ui::WindowSize size = engine::ui::window_size_for(world, canvas.window);
        engine::ui::apply_layout_style(instance.document.root, sheet, static_cast<float>(size.width),
                                       static_cast<float>(size.height));
        engine::ui::layout(instance.document, canvas.rect);
    }

    engine::ecs::Entity profiler_panel(engine::ecs::World &world) {
        engine::ecs::Entity found{};
        auto view = world.view<engine::ui::ProfilerPanel>();
        for (engine::ecs::Entity entity: view) {
            found = entity;
        }
        return found;
    }

    void paint_canvas(engine::ecs::World &world, engine::ecs::Entity entity, NullPainter &painter) {
        engine::ui::UiInstance &instance = world.get<engine::ui::UiInstance>(entity);
        engine::ui::paint_document(instance.document, nullptr, painter,
                                   engine::ui::UiPaintInput{
                                           .canvas_rect = {0.0f, 0.0f, 120.0f, 80.0f},
                                           .window_width = 120.0f,
                                           .window_height = 80.0f,
                                           .canvas = entity,
                                   });
    }

} // namespace

#if defined(ENGINE_UI_PROFILER)

TEST(UiProfiler, ClosedProfilerStoresNothing) {
    engine::ecs::World world;
    const engine::ecs::Entity hud = spawn_named(world, "hud", true);
    NullPainter painter;
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    const engine::ui::ProfileSample sample = engine::ui::profiler_canvas_sample(world, hud);
    EXPECT_FALSE(sample.stored);
    EXPECT_EQ(sample.frames, 0);
    EXPECT_FALSE(engine::ui::profiler_shared_sample(world).stored);
}

TEST(UiProfiler, BeginFrameIsStoredOnlyWhileOpen) {
    engine::ecs::World world;
    engine::ui::set_ui_profiler_enabled(world, true);
    engine::ui::begin_frame(world);
    engine::ui::begin_frame(world);
    const engine::ui::ProfileSample shared = engine::ui::profiler_shared_sample(world);
    EXPECT_TRUE(shared.stored);
    EXPECT_GE(shared.frames, 1);
    engine::ui::set_ui_profiler_enabled(world, false);
    EXPECT_FALSE(engine::ui::profiler_shared_sample(world).stored);
    EXPECT_FALSE(engine::ui::ui_profiler_enabled(world));
}

TEST(UiProfiler, BindSamplesStayOnTheirCanvas) {
    engine::ecs::World world;
    const engine::ecs::Entity alpha = spawn_named(world, "alpha", false);
    const engine::ecs::Entity beta = spawn_named(world, "beta", false);
    engine::ui::set_inspector_enabled(world, true);
    engine::ui::set_ui_profiler_enabled(world, true);
    const engine::ecs::Entity inspector = [&] {
        engine::ecs::Entity found{};
        auto view = world.view<engine::ui::InspectorPanel>();
        for (engine::ecs::Entity entity: view) {
            found = entity;
        }
        return found;
    }();
    const engine::ecs::Entity panel = profiler_panel(world);
    ASSERT_TRUE(world.valid(inspector));
    ASSERT_TRUE(world.valid(panel));

    engine::register_engine_systems(world);
    world.run(engine::ecs::Schedule::Frame);
    engine::ui::begin_frame(world);

    const engine::ui::ProfileSample alpha_sample = engine::ui::profiler_canvas_sample(world, alpha);
    const engine::ui::ProfileSample beta_sample = engine::ui::profiler_canvas_sample(world, beta);
    EXPECT_TRUE(alpha_sample.stored);
    EXPECT_TRUE(alpha_sample.saw_bindings);
    EXPECT_TRUE(beta_sample.stored);
    EXPECT_TRUE(beta_sample.saw_bindings);
    EXPECT_FALSE(engine::ui::profiler_canvas_sample(world, inspector).stored);
    EXPECT_FALSE(engine::ui::profiler_canvas_sample(world, panel).stored);
    engine::ui::set_ui_profiler_enabled(world, false);
    EXPECT_FALSE(engine::ui::profiler_canvas_sample(world, alpha).stored);
}

TEST(UiProfiler, PaintRecordsLayoutThenASkip) {
    engine::ecs::World world;
    const engine::ecs::Entity hud = spawn_named(world, "hud", true);
    engine::ui::set_ui_profiler_enabled(world, true);
    NullPainter painter;
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    const engine::ui::ProfileSample first = engine::ui::profiler_canvas_sample(world, hud);
    EXPECT_TRUE(first.stored);
    EXPECT_TRUE(first.saw_paint);
    EXPECT_TRUE(first.layout_ran);
    EXPECT_GE(first.elements, 2);
    EXPECT_EQ(first.generated, 0);

    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    const engine::ui::ProfileSample second = engine::ui::profiler_canvas_sample(world, hud);
    EXPECT_EQ(second.frames, 2);
    EXPECT_TRUE(second.saw_paint);
    EXPECT_FALSE(second.layout_ran);
    EXPECT_GE(second.elements, 2);

    engine::ui::sync_profiler_content(world);
    const engine::ecs::Entity panel = profiler_panel(world);
    layout_instance(world, panel);
    engine::ui::Element *stats = find_id(world.get<engine::ui::UiInstance>(panel).document.root, "stats");
    ASSERT_NE(stats, nullptr);
    EXPECT_NE(stats->text.find("skipped"), std::string::npos);
    engine::ui::set_ui_profiler_enabled(world, false);
}

TEST(UiProfiler, PauseKeepsTheDisplayedFrame) {
    engine::ecs::World world;
    const engine::ecs::Entity hud = spawn_named(world, "hud", true);
    engine::ui::set_ui_profiler_enabled(world, true);
    NullPainter painter;
    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    EXPECT_TRUE(engine::ui::profiler_canvas_sample(world, hud).layout_ran);

    const engine::ecs::Entity panel = profiler_panel(world);
    layout_instance(world, panel);
    engine::ui::UiInstance &panel_instance = world.get<engine::ui::UiInstance>(panel);
    engine::ui::Element *box = find_id(panel_instance.document.root, "pause");
    ASSERT_NE(box, nullptr);
    ASSERT_GT(box->layout_rect.w, 1.0f);
    const engine::WindowId panel_window = world.get<engine::ui::UiCanvas>(panel).window;
    engine::ui::handle_pointer(world, box->layout_rect.x + box->layout_rect.w * 0.5f,
                               box->layout_rect.y + box->layout_rect.h * 0.5f, panel_window);
    engine::ui::sync_profiler_content(world);

    paint_canvas(world, hud, painter);
    engine::ui::begin_frame(world);
    const engine::ui::ProfileSample held = engine::ui::profiler_canvas_sample(world, hud);
    EXPECT_EQ(held.frames, 1);
    EXPECT_TRUE(held.layout_ran);
    engine::ui::set_ui_profiler_enabled(world, false);
    EXPECT_FALSE(world.valid(profiler_panel(world)));
}

TEST(UiProfiler, RowClickSelectsThatCanvas) {
    engine::ecs::World world;
    const engine::ecs::Entity alpha = spawn_named(world, "alpha", false);
    const engine::ecs::Entity beta = spawn_named(world, "beta", false);
    engine::ui::set_ui_profiler_enabled(world, true);
    EXPECT_EQ(engine::ui::profiler_selected(world), alpha);

    const engine::ecs::Entity panel = profiler_panel(world);
    layout_instance(world, panel);
    engine::ui::UiInstance &panel_instance = world.get<engine::ui::UiInstance>(panel);
    engine::ui::Element *row = find_text(panel_instance.document.root, "beta");
    ASSERT_NE(row, nullptr);
    ASSERT_GT(row->layout_rect.w, 1.0f);
    const engine::WindowId panel_window = world.get<engine::ui::UiCanvas>(panel).window;
    engine::ui::handle_pointer(world, row->layout_rect.x + row->layout_rect.w * 0.5f,
                               row->layout_rect.y + row->layout_rect.h * 0.5f, panel_window);
    EXPECT_EQ(engine::ui::profiler_selected(world), beta);
    engine::ui::set_ui_profiler_enabled(world, false);
}

TEST(UiProfiler, HostOpensOneWindowAndDisableClosesIt) {
    engine::ecs::World world;
    int opens = 0;
    int closes = 0;
    std::string title;
    bool resizable = false;
    world.ctx<engine::ui::ProfilerWindowHost>().open = [&](const engine::WindowDesc &desc) {
        ++opens;
        title = desc.title;
        resizable = desc.style.resizable;
        return engine::WindowId{9};
    };
    world.ctx<engine::ui::ProfilerWindowHost>().close = [&](engine::WindowId id) {
        ++closes;
        EXPECT_EQ(id, engine::WindowId{9});
    };

    engine::ui::set_ui_profiler_enabled(world, true);
    EXPECT_EQ(opens, 1);
    EXPECT_EQ(title, "UI Profiler");
    EXPECT_TRUE(resizable);
    const engine::ecs::Entity panel = profiler_panel(world);
    ASSERT_TRUE(world.valid(panel));
    EXPECT_EQ(world.get<engine::ui::UiCanvas>(panel).window, engine::WindowId{9});
    EXPECT_FALSE(world.ctx<engine::ui::WindowSizes>().sizes.contains(engine::WindowId{9}));

    engine::ui::set_ui_profiler_enabled(world, false);
    EXPECT_EQ(closes, 1);
    EXPECT_FALSE(engine::ui::ui_profiler_enabled(world));
    EXPECT_FALSE(world.valid(profiler_panel(world)));
}

TEST(UiProfiler, CloseRequestDisablesAndClosesTheWindow) {
    engine::ecs::World world;
    int closes = 0;
    world.ctx<engine::ui::ProfilerWindowHost>().open = [](const engine::WindowDesc &) { return engine::WindowId{9}; };
    world.ctx<engine::ui::ProfilerWindowHost>().close = [&](engine::WindowId) { ++closes; };

    engine::ui::set_ui_profiler_enabled(world, true);
    engine::ecs::EventWriter<engine::ui::WindowCloseRequestedEvent>{world}.send(
            engine::ui::WindowCloseRequestedEvent{.window = engine::WindowId{9}});
    engine::ui::begin_frame(world);
    EXPECT_FALSE(engine::ui::ui_profiler_enabled(world));
    EXPECT_EQ(closes, 1);

    engine::ui::begin_frame(world);
    EXPECT_EQ(closes, 1);
}

#else

TEST(UiProfiler, ToggleIsANoOp) {
    engine::ecs::World world;
    engine::ui::set_ui_profiler_enabled(world, true);
    EXPECT_FALSE(engine::ui::ui_profiler_enabled(world));
}

#endif
