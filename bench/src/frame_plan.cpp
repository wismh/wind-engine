#include "frame_plan.h"

namespace bench {

FramePlan plan_frame(BenchMode mode, int step, std::size_t hover_targets) {
    FramePlan plan;
    plan.step = step;
    switch (mode) {
        case BenchMode::Quiet:
            break;
        case BenchMode::OneChange:
            plan.change_value = true;
            break;
        case BenchMode::Hover:
            if (hover_targets > 0 && step >= 0) {
                plan.move_pointer = true;
                plan.hover_target = static_cast<std::size_t>(step) % hover_targets;
            }
            break;
        case BenchMode::Scroll:
            plan.wheel = kScrollWheel;
            break;
        case BenchMode::Churn:
            plan.churn = true;
            break;
    }
    return plan;
}

}
