#pragma once

// docs/tech/modules/ECS.md

namespace engine {

enum class BodyType {
    Static,     // does not integrate; inverse mass 0
    Kinematic,  // default; parent-space velocity; the solver does not change it
    Dynamic,    // world velocity, gravity, positional response
};

enum class CollisionPhase {
    Enter,
    Stay,
    Exit,
};

inline constexpr float kMinDynamicMass = 1.0e-4f;
inline constexpr float kContactSlop = 1.0e-3f;
// Restitution is 0 when the approach is this slow or slower (v_rel_n >= -kBounceThreshold).
inline constexpr float kBounceThreshold = 1.f;
inline constexpr int kPositionIterations = 8;
inline constexpr float kSleepLinear = 0.05f;
inline constexpr float kSleepAngular = 0.05f;
inline constexpr int kSleepSteps = 8;

}
