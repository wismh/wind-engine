#include "bench_app.h"

#include "bench_matrix.h"
#include "bench_report.h"
#include "frame_plan.h"
#include "scene_factory.h"
#include "scene_points.h"

#include <engine/core/build_info.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/profiler.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <optional>

#if !defined(WIND_BENCH_CONFIG) || !defined(WIND_BENCH_ENGINE_VERSION) || !defined(WIND_BENCH_COMMIT) ||               \
        !defined(WIND_BENCH_DIRTY)
#error "bench/CMakeLists.txt defines the build configuration, engine version, and git commit of the report"
#endif

namespace bench {
namespace {

constexpr std::string_view kRestId = "rest";
constexpr std::string_view kWheelId = "wheel";

}

BenchApp::BenchApp(const engine::EngineServices& services, const BenchCase& bench_case, const BenchOptions& options)
    : engine::GameBase(services.worlds)
    , input_(&services.input)
    , windows_(&services.windows)
    , case_(bench_case)
    , options_(options)
    , schedule_{.warmup = options.warmup, .frames = options.frames}
    , scene_(make_scene(bench_case)) {}

engine::WindowDesc BenchApp::primary_window() const {
    return engine::WindowDesc{
            .title = "Wind UI Bench: " + bench_case_name(case_),
            .size = {options_.width, options_.height},
    };
}

void BenchApp::on_start() {
    // CPU stage times are the point, not frames per second: no swap waits for the display.
    windows_->set_vsync(false);
    input_->set_router([this](engine::WindowId window) -> engine::ecs::World* {
        return injecting_ && window == engine::kPrimaryWindow ? &world() : nullptr;
    });

    engine::ecs::World& ui = world();
    const std::vector<engine::AssetId> sheets = scene_->stylesheets();
    canvas_ = ui.create();
    ui.emplace<engine::ui::UiCanvas>(canvas_, engine::ui::UiCanvas{
            .document = scene_->document(),
            .stylesheet = sheets.front(),
            .extra_stylesheets = std::vector<engine::AssetId>(sheets.begin() + 1, sheets.end()),
            .data_context = scene_->view_model(),
            .fit = engine::ui::UiFit::FillWindow,
    });
    engine::ui::set_ui_profiler_attached(ui, true);
    ui.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game, [this](engine::ecs::World&) { frame(); });
}

void BenchApp::frame() {
    if (finished_) {
        return;
    }
    const int tick = tick_++;
    const int stored = tick > schedule_.warmup ? stored_frames() : 0;
    switch (schedule_.at(tick, stored)) {
        case BenchStep::Wait:
            return;
        case BenchStep::Setup:
            set_up();
            return;
        case BenchStep::Warmup:
        case BenchStep::Measure:
            drive(tick);
            return;
        case BenchStep::Clear:
            engine::ui::profiler_clear(world());
            drive(tick);
            return;
        case BenchStep::Finish:
            finish();
            return;
        case BenchStep::Fail:
            fail(std::format("only {} of {} frames were recorded", stored, schedule_.frames));
            return;
    }
}

void BenchApp::set_up() {
    engine::ui::UiInstance* instance = world().try_get<engine::ui::UiInstance>(canvas_);
    if (instance == nullptr) {
        fail("the scene document did not load");
        return;
    }
    const engine::ui::WindowSize size = engine::ui::window_size_for(world(), engine::kPrimaryWindow);
    const engine::render::Rect bounds{0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height)};
    // The engine's bind pass only logs this error and binds the rest; a run on a tree with a hole measures the wrong
    // scene.
    if (!engine::ui::apply_bindings(instance->document, *scene_->view_model())) {
        fail("a binding of the document is not registered on its view-model");
        return;
    }
    engine::ui::Element& root = instance->document.root;

    const std::optional<glm::vec2> rest = element_point(root, kRestId, bounds);
    if (!rest) {
        fail("the scene has no visible element with id \"rest\"");
        return;
    }
    rest_point_ = *rest;
    if (case_.mode == BenchMode::Scroll) {
        const std::optional<glm::vec2> wheel = element_point(root, kWheelId, bounds);
        if (!wheel) {
            fail("the scene has no visible element with id \"wheel\"");
            return;
        }
        wheel_point_ = *wheel;
    }
    if (case_.mode == BenchMode::Hover) {
        hover_points_ = hover_points(root, bounds);
        if (hover_points_.size() < 2) {
            fail(std::format("hover needs two interactive elements on screen, the scene has {}", hover_points_.size()));
            return;
        }
    }
    // The pointer rests on a spot that is not interactive; hover moves it from there.
    inject_move(rest_point_);
}

void BenchApp::drive(int tick) {
    const FramePlan plan = plan_frame(case_.mode, BenchSchedule::mode_step(tick), hover_points_.size());
    if (plan.change_value) {
        scene_->change_value(plan.step);
    }
    if (plan.move_pointer) {
        inject_move(hover_points_[plan.hover_target]);
    }
    if (plan.wheel != 0.0f) {
        inject_wheel(wheel_point_, plan.wheel);
    }
    if (plan.churn) {
        scene_->churn(plan.step);
    }
}

void BenchApp::finish() {
    finished_ = true;
    const engine::ui::WindowSize size = engine::ui::window_size_for(world(), engine::kPrimaryWindow);
    BenchReport report;
    report.bench_case = case_;
    report.warmup = options_.warmup;
    report.frames = options_.frames;
    report.width = options_.width;
    report.height = options_.height;
    report.drawable_width = size.width;
    report.drawable_height = size.height;
    report.vsync = windows_->vsync();
    report.hover_targets = hover_points_.size();
    report.config = WIND_BENCH_CONFIG;
    report.engine_version = WIND_BENCH_ENGINE_VERSION;
    report.build_id = std::string(engine::build_id());
    report.commit = WIND_BENCH_COMMIT;
    report.dirty = WIND_BENCH_DIRTY != 0;
    report.timestamp = utc_timestamp(std::chrono::system_clock::now());
    const std::string json = bench_report_json(report, engine::ui::profiler_json(world()));

    const std::filesystem::path path = options_.out.empty() ? default_report_path(case_) : options_.out;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << json;
    file.close();
    if (!file) {
        fail("cannot write " + path.string());
        return;
    }
    std::printf("wind_ui_bench: %s: %d frames written to %s\n", bench_case_name(case_).c_str(), options_.frames,
            path.string().c_str());
    exit_code_ = 0;
    worlds().application_state().quit();
}

void BenchApp::fail(const std::string& reason) {
    finished_ = true;
    std::fprintf(stderr, "wind_ui_bench: %s: %s\n", bench_case_name(case_).c_str(), reason.c_str());
    exit_code_ = 1;
    worlds().application_state().quit();
}

void BenchApp::inject_move(glm::vec2 position) {
    injecting_ = true;
    input_->handle_mouse_move(engine::kPrimaryWindow, position, glm::vec2{0.0f});
    injecting_ = false;
}

void BenchApp::inject_wheel(glm::vec2 position, float wheel_y) {
    injecting_ = true;
    input_->handle_mouse_wheel(engine::kPrimaryWindow, position, wheel_y);
    injecting_ = false;
}

int BenchApp::stored_frames() {
    for (const engine::ui::ProfilerCanvas& canvas : engine::ui::profiler_canvases(world())) {
        if (canvas.canvas == canvas_) {
            return canvas.frames;
        }
    }
    return 0;
}

}
