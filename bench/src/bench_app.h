#pragma once

#include "bench_case.h"
#include "bench_options.h"
#include "bench_schedule.h"
#include "scene.h"

#include <engine/core/engine_services.h>
#include <engine/core/input_system.h>
#include <engine/core/window_control.h>
#include <engine/ecs/entity.h>
#include <engine/igame.h>

#include <glm/vec2.hpp>

#include <memory>
#include <string>
#include <vector>

namespace bench {

// wind_ui_bench: one scene in one mode on one canvas of its own world. Attaches the UI profiler to that world, turns
// vsync off, and owns the pointer: the input router passes only the events the bench injects through InputSystem, so
// the real mouse over the window changes nothing. Its frame system runs BenchSchedule: warmup, clear the rings,
// measure, write the report, quit. Holds `this` in its system and router, so it never moves.
class BenchApp final : public engine::GameBase {
public:
    BenchApp(const engine::EngineServices& services, const BenchCase& bench_case, const BenchOptions& options);

    BenchApp(const BenchApp&) = delete;
    BenchApp& operator=(const BenchApp&) = delete;

    [[nodiscard]] engine::WindowDesc primary_window() const override;
    void on_start() override;

    // 0 once the report is written; 1 when the frames never came or the report could not be written.
    [[nodiscard]] int exit_code() const { return exit_code_; }

private:
    void frame();
    void set_up();
    void drive(int tick);
    void finish();
    void fail(const std::string& reason);
    void inject_move(glm::vec2 position);
    void inject_wheel(glm::vec2 position, float wheel_y);
    [[nodiscard]] int stored_frames();

    engine::InputSystem* input_;
    engine::IWindowControl* windows_;
    BenchCase case_;
    BenchOptions options_;
    BenchSchedule schedule_;
    std::unique_ptr<IScene> scene_;
    engine::ecs::Entity canvas_{};

    int tick_ = 0;
    bool finished_ = false;
    // True only while the bench calls InputSystem: the router drops every other event.
    bool injecting_ = false;
    glm::vec2 rest_point_{};
    glm::vec2 wheel_point_{};
    std::vector<glm::vec2> hover_points_;
    int exit_code_ = 1;
};

}
