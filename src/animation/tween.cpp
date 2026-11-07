#include <engine/animation/tween.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/render/sprite.h>

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace engine::animation {

float evaluate_ease(Ease ease, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);

    switch (ease) {
        case Ease::Linear:
            return t;

        case Ease::InQuad:
            return t * t;

        case Ease::OutQuad:
            return t * (2.0f - t);

        case Ease::InOutQuad:
            return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;

        case Ease::InCubic:
            return t * t * t;

        case Ease::OutCubic: {
            const float f = t - 1.0f;
            return f * f * f + 1.0f;
        }

        case Ease::InOutCubic: {
            if (t < 0.5f) {
                return 4.0f * t * t * t;
            }
            const float f = 2.0f * t - 2.0f;
            return 0.5f * f * f * f + 1.0f;
        }

        case Ease::InSine:
            return 1.0f - std::cos(t * (static_cast<float>(std::numbers::pi) * 0.5f));

        case Ease::OutSine:
            return std::sin(t * (static_cast<float>(std::numbers::pi) * 0.5f));

        case Ease::InOutSine:
            return -0.5f * (std::cos(static_cast<float>(std::numbers::pi) * t) - 1.0f);

        case Ease::OutBounce: {
            constexpr float kN1 = 7.5625f;
            constexpr float kD1 = 2.75f;

            if (t < 1.0f / kD1) {
                return kN1 * t * t;
            }
            if (t < 2.0f / kD1) {
                const float s = t - 1.5f / kD1;
                return kN1 * s * s + 0.75f;
            }
            if (t < 2.5f / kD1) {
                const float s = t - 2.25f / kD1;
                return kN1 * s * s + 0.9375f;
            }
            const float s = t - 2.625f / kD1;
            return kN1 * s * s + 0.984375f;
        }

        case Ease::InBounce:
            return 1.0f - evaluate_ease(Ease::OutBounce, 1.0f - t);

        case Ease::InOutBounce:
            if (t < 0.5f) {
                return (1.0f - evaluate_ease(Ease::OutBounce, 1.0f - 2.0f * t)) * 0.5f;
            }
            return (1.0f + evaluate_ease(Ease::OutBounce, 2.0f * t - 1.0f)) * 0.5f;

        case Ease::SmoothStep:
            return t * t * (3.0f - 2.0f * t);
    }

    return t;
}

void run_tween_transforms(ecs::World& world) {
    const float dt = world.ctx<Time>().delta_time;
    std::vector<ecs::Entity> finished;

    auto view = world.view<TweenTransform, Transform>();
    for (ecs::Entity entity : view) {
        auto& tween = view.get<TweenTransform>(entity);
        auto& transform = view.get<Transform>(entity);

        if (!tween.initialized) {
            if (has_channel(tween.channels, TransformTweenChannels::Position) &&
                    tween.start_position == tween.target_position) {
                tween.start_position = transform.position;
            }
            if (has_channel(tween.channels, TransformTweenChannels::Rotation) &&
                    tween.start_rotation == tween.target_rotation) {
                tween.start_rotation = transform.rotation;
            }
            if (has_channel(tween.channels, TransformTweenChannels::Scale) &&
                    tween.start_scale == tween.target_scale) {
                tween.start_scale = transform.scale;
            }
            tween.initialized = true;
        }

        tween.elapsed += dt;

        float effective_t = 1.0f;
        bool is_done = false;

        if (tween.duration <= 0.0f) {
            is_done = true;
            effective_t = 1.0f;
        } else if (tween.loop == LoopType::Once) {
            if (tween.elapsed >= tween.duration) {
                is_done = true;
                effective_t = 1.0f;
            } else {
                const float progress = tween.elapsed / tween.duration;
                effective_t = evaluate_ease(tween.ease, progress);
            }
        } else if (tween.loop == LoopType::Loop) {
            while (tween.elapsed >= tween.duration) {
                tween.elapsed -= tween.duration;
            }
            const float progress = tween.elapsed / tween.duration;
            effective_t = evaluate_ease(tween.ease, progress);
        } else if (tween.loop == LoopType::Yoyo) {
            while (tween.elapsed >= tween.duration) {
                tween.elapsed -= tween.duration;
                tween.yoyo_direction = !tween.yoyo_direction;
            }
            const float progress = tween.elapsed / tween.duration;
            const float eased_t = evaluate_ease(tween.ease, progress);
            effective_t = tween.yoyo_direction ? (1.0f - eased_t) : eased_t;
        }

        if (has_channel(tween.channels, TransformTweenChannels::Position)) {
            transform.position = glm::mix(tween.start_position, tween.target_position, effective_t);
        }

        if (has_channel(tween.channels, TransformTweenChannels::Rotation)) {
            glm::quat q_target = tween.target_rotation;
            if (glm::dot(tween.start_rotation, q_target) < 0.0f) {
                q_target = -q_target;
            }
            transform.rotation = glm::slerp(tween.start_rotation, q_target, effective_t);
        }

        if (has_channel(tween.channels, TransformTweenChannels::Scale)) {
            transform.scale = glm::mix(tween.start_scale, tween.target_scale, effective_t);
        }

        if (is_done) {
            finished.push_back(entity);
        }
    }

    if (!finished.empty()) {
        ecs::EventWriter<TweenCompletedEvent> writer{world};
        for (ecs::Entity entity : finished) {
            world.remove<TweenTransform>(entity);
            writer.send(TweenCompletedEvent{
                .entity = entity,
                .kind = TweenKind::Transform,
            });
        }
    }
}

void run_tween_colors(ecs::World& world) {
    const float dt = world.ctx<Time>().delta_time;
    std::vector<ecs::Entity> finished;

    auto view = world.view<TweenColor, render::Sprite>();
    for (ecs::Entity entity : view) {
        auto& tween = view.get<TweenColor>(entity);
        auto& sprite = view.get<render::Sprite>(entity);

        if (!tween.initialized) {
            if (tween.start_color == tween.target_color) {
                tween.start_color = sprite.color;
            }
            tween.initialized = true;
        }

        tween.elapsed += dt;

        float effective_t = 1.0f;
        bool is_done = false;

        if (tween.duration <= 0.0f) {
            is_done = true;
            effective_t = 1.0f;
        } else if (tween.loop == LoopType::Once) {
            if (tween.elapsed >= tween.duration) {
                is_done = true;
                effective_t = 1.0f;
            } else {
                const float progress = tween.elapsed / tween.duration;
                effective_t = evaluate_ease(tween.ease, progress);
            }
        } else if (tween.loop == LoopType::Loop) {
            while (tween.elapsed >= tween.duration) {
                tween.elapsed -= tween.duration;
            }
            const float progress = tween.elapsed / tween.duration;
            effective_t = evaluate_ease(tween.ease, progress);
        } else if (tween.loop == LoopType::Yoyo) {
            while (tween.elapsed >= tween.duration) {
                tween.elapsed -= tween.duration;
                tween.yoyo_direction = !tween.yoyo_direction;
            }
            const float progress = tween.elapsed / tween.duration;
            const float eased_t = evaluate_ease(tween.ease, progress);
            effective_t = tween.yoyo_direction ? (1.0f - eased_t) : eased_t;
        }

        sprite.color = glm::mix(tween.start_color, tween.target_color, effective_t);

        if (is_done) {
            finished.push_back(entity);
        }
    }

    if (!finished.empty()) {
        ecs::EventWriter<TweenCompletedEvent> writer{world};
        for (ecs::Entity entity : finished) {
            world.remove<TweenColor>(entity);
            writer.send(TweenCompletedEvent{
                .entity = entity,
                .kind = TweenKind::Color,
            });
        }
    }
}

void run_tweens(ecs::World& world) {
    run_tween_transforms(world);
    run_tween_colors(world);
}

void tween_position(ecs::World& world, ecs::Entity entity, const glm::vec3& target,
        float duration, Ease ease, LoopType loop) {
    auto* transform = world.try_get<Transform>(entity);
    const glm::vec3 start = transform ? transform->position : glm::vec3{0.0f};

    if (auto* existing = world.try_get<TweenTransform>(entity)) {
        existing->start_position = start;
        existing->target_position = target;
        existing->duration = duration;
        existing->elapsed = 0.0f;
        existing->ease = ease;
        existing->loop = loop;
        existing->channels = existing->channels | TransformTweenChannels::Position;
        existing->yoyo_direction = false;
        existing->initialized = true;
    } else {
        world.emplace<TweenTransform>(entity, TweenTransform{
            .start_position = start,
            .target_position = target,
            .duration = duration,
            .elapsed = 0.0f,
            .ease = ease,
            .loop = loop,
            .channels = TransformTweenChannels::Position,
            .yoyo_direction = false,
            .initialized = true,
        });
    }
}

void tween_scale(ecs::World& world, ecs::Entity entity, const glm::vec3& target,
        float duration, Ease ease, LoopType loop) {
    auto* transform = world.try_get<Transform>(entity);
    const glm::vec3 start = transform ? transform->scale : glm::vec3{1.0f};

    if (auto* existing = world.try_get<TweenTransform>(entity)) {
        existing->start_scale = start;
        existing->target_scale = target;
        existing->duration = duration;
        existing->elapsed = 0.0f;
        existing->ease = ease;
        existing->loop = loop;
        existing->channels = existing->channels | TransformTweenChannels::Scale;
        existing->yoyo_direction = false;
        existing->initialized = true;
    } else {
        world.emplace<TweenTransform>(entity, TweenTransform{
            .start_scale = start,
            .target_scale = target,
            .duration = duration,
            .elapsed = 0.0f,
            .ease = ease,
            .loop = loop,
            .channels = TransformTweenChannels::Scale,
            .yoyo_direction = false,
            .initialized = true,
        });
    }
}

void tween_rotation(ecs::World& world, ecs::Entity entity, const glm::quat& target,
        float duration, Ease ease, LoopType loop) {
    auto* transform = world.try_get<Transform>(entity);
    const glm::quat start = transform ? transform->rotation : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};

    if (auto* existing = world.try_get<TweenTransform>(entity)) {
        existing->start_rotation = start;
        existing->target_rotation = target;
        existing->duration = duration;
        existing->elapsed = 0.0f;
        existing->ease = ease;
        existing->loop = loop;
        existing->channels = existing->channels | TransformTweenChannels::Rotation;
        existing->yoyo_direction = false;
        existing->initialized = true;
    } else {
        world.emplace<TweenTransform>(entity, TweenTransform{
            .start_rotation = start,
            .target_rotation = target,
            .duration = duration,
            .elapsed = 0.0f,
            .ease = ease,
            .loop = loop,
            .channels = TransformTweenChannels::Rotation,
            .yoyo_direction = false,
            .initialized = true,
        });
    }
}

void tween_color(ecs::World& world, ecs::Entity entity, const glm::vec4& target,
        float duration, Ease ease, LoopType loop) {
    auto* sprite = world.try_get<render::Sprite>(entity);
    const glm::vec4 start = sprite ? sprite->color : glm::vec4{1.0f};

    if (auto* existing = world.try_get<TweenColor>(entity)) {
        existing->start_color = start;
        existing->target_color = target;
        existing->duration = duration;
        existing->elapsed = 0.0f;
        existing->ease = ease;
        existing->loop = loop;
        existing->yoyo_direction = false;
        existing->initialized = true;
    } else {
        world.emplace<TweenColor>(entity, TweenColor{
            .start_color = start,
            .target_color = target,
            .duration = duration,
            .elapsed = 0.0f,
            .ease = ease,
            .loop = loop,
            .yoyo_direction = false,
            .initialized = true,
        });
    }
}

void tween_fade(ecs::World& world, ecs::Entity entity, float target_alpha,
        float duration, Ease ease, LoopType loop) {
    auto* sprite = world.try_get<render::Sprite>(entity);
    glm::vec4 start = sprite ? sprite->color : glm::vec4{1.0f};
    glm::vec4 target = start;
    target.a = target_alpha;

    if (auto* existing = world.try_get<TweenColor>(entity)) {
        existing->start_color = start;
        existing->target_color = target;
        existing->duration = duration;
        existing->elapsed = 0.0f;
        existing->ease = ease;
        existing->loop = loop;
        existing->yoyo_direction = false;
        existing->initialized = true;
    } else {
        world.emplace<TweenColor>(entity, TweenColor{
            .start_color = start,
            .target_color = target,
            .duration = duration,
            .elapsed = 0.0f,
            .ease = ease,
            .loop = loop,
            .yoyo_direction = false,
            .initialized = true,
        });
    }
}

} // namespace engine::animation
