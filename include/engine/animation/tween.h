#pragma once

#include <engine/core/export.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace engine::animation {

enum class Ease : std::uint8_t {
    Linear = 0,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InSine,
    OutSine,
    InOutSine,
    InBounce,
    OutBounce,
    InOutBounce,
    SmoothStep,
};

enum class LoopType : std::uint8_t {
    Once = 0,
    Loop,
    Yoyo,
};

enum class TransformTweenChannels : std::uint8_t {
    None = 0,
    Position = 1 << 0,
    Rotation = 1 << 1,
    Scale = 1 << 2,
    All = Position | Rotation | Scale,
};

[[nodiscard]] constexpr TransformTweenChannels operator|(
        TransformTweenChannels a, TransformTweenChannels b) noexcept {
    return static_cast<TransformTweenChannels>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr TransformTweenChannels operator&(
        TransformTweenChannels a, TransformTweenChannels b) noexcept {
    return static_cast<TransformTweenChannels>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr bool has_channel(
        TransformTweenChannels mask, TransformTweenChannels channel) noexcept {
    return (static_cast<std::uint8_t>(mask) & static_cast<std::uint8_t>(channel)) != 0;
}

struct TweenTransform {
    glm::vec3 start_position{0.0f};
    glm::vec3 target_position{0.0f};

    glm::quat start_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat target_rotation{1.0f, 0.0f, 0.0f, 0.0f};

    glm::vec3 start_scale{1.0f};
    glm::vec3 target_scale{1.0f};

    float duration = 1.0f;
    float elapsed = 0.0f;
    Ease ease = Ease::OutQuad;
    LoopType loop = LoopType::Once;
    TransformTweenChannels channels = TransformTweenChannels::Position;
    bool yoyo_direction = false;
    bool initialized = false;
};

struct TweenColor {
    glm::vec4 start_color{1.0f};
    glm::vec4 target_color{1.0f};

    float duration = 1.0f;
    float elapsed = 0.0f;
    Ease ease = Ease::Linear;
    LoopType loop = LoopType::Once;
    bool yoyo_direction = false;
    bool initialized = false;
};

enum class TweenKind : std::uint8_t {
    Transform,
    Color,
};

struct TweenCompletedEvent {
    ecs::Entity entity;
    TweenKind kind = TweenKind::Transform;
};

[[nodiscard]] ENGINE_API float evaluate_ease(Ease ease, float t) noexcept;

ENGINE_API void run_tween_transforms(ecs::World& world);
ENGINE_API void run_tween_colors(ecs::World& world);
ENGINE_API void run_tweens(ecs::World& world);

ENGINE_API void tween_position(ecs::World& world, ecs::Entity entity, const glm::vec3& target,
        float duration, Ease ease = Ease::OutQuad, LoopType loop = LoopType::Once);

ENGINE_API void tween_scale(ecs::World& world, ecs::Entity entity, const glm::vec3& target,
        float duration, Ease ease = Ease::OutQuad, LoopType loop = LoopType::Once);

ENGINE_API void tween_rotation(ecs::World& world, ecs::Entity entity, const glm::quat& target,
        float duration, Ease ease = Ease::OutQuad, LoopType loop = LoopType::Once);

ENGINE_API void tween_color(ecs::World& world, ecs::Entity entity, const glm::vec4& target,
        float duration, Ease ease = Ease::Linear, LoopType loop = LoopType::Once);

ENGINE_API void tween_fade(ecs::World& world, ecs::Entity entity, float target_alpha,
        float duration, Ease ease = Ease::Linear, LoopType loop = LoopType::Once);

} // namespace engine::animation
