#pragma once

namespace engine {

inline constexpr float kFixed = 1.0f / 60.0f;
inline constexpr int kMaxFixedSteps = 8;
inline constexpr float kMaxFrameDt = 0.25f;

struct Time {
    static constexpr float kFixed = engine::kFixed;
    static constexpr int kMaxFixedSteps = engine::kMaxFixedSteps;
    static constexpr float kMaxFrameDt = engine::kMaxFrameDt;

    float delta_time = 0.0f;
    float fixed_delta_time = 0.0f;
    float alpha = 0.0f;
    float accumulator = 0.0f;
};

}
