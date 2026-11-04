#pragma once

// docs/tech/modules/Core.md

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
    float total_time = 0.0f;
    float alpha = 0.0f;
    float accumulator = 0.0f;
    // Scales the wall time fed to the fixed-step accumulator; `fixed_delta_time` stays `kFixed`. Per world, 1 by
    // default. Only a net client's prediction changes it (Net.md, client lead), and a disconnect resets it to 1.
    float fixed_rate = 1.0f;
};

}
