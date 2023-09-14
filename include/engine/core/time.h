#pragma once

namespace engine {

inline constexpr float FIXED = 1.0f / 60.0f;
inline constexpr int MAX_FIXED_STEPS = 8;
inline constexpr float MAX_FRAME_DT = 0.25f;

struct Time {
    static constexpr float FIXED = engine::FIXED;
    static constexpr int MAX_FIXED_STEPS = engine::MAX_FIXED_STEPS;
    static constexpr float MAX_FRAME_DT = engine::MAX_FRAME_DT;

    float deltaTime = 0.0f;
    float fixedDeltaTime = 0.0f;
    float alpha = 0.0f;
    float accumulator = 0.0f;
};

}
