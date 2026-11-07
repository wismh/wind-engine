#include <gtest/gtest.h>

#include <engine/animation/tween.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/sprite.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

using namespace engine::animation;

TEST(TweenTest, EasingBoundsAndContinuity) {
    const std::vector<Ease> all_eases = {
        Ease::Linear,
        Ease::InQuad,
        Ease::OutQuad,
        Ease::InOutQuad,
        Ease::InCubic,
        Ease::OutCubic,
        Ease::InOutCubic,
        Ease::InSine,
        Ease::OutSine,
        Ease::InOutSine,
        Ease::InBounce,
        Ease::OutBounce,
        Ease::InOutBounce,
        Ease::SmoothStep,
    };

    for (Ease ease : all_eases) {
        EXPECT_NEAR(evaluate_ease(ease, 0.0f), 0.0f, 1e-4f);
        EXPECT_NEAR(evaluate_ease(ease, 1.0f), 1.0f, 1e-4f);
        const float mid = evaluate_ease(ease, 0.5f);
        EXPECT_GE(mid, -0.1f);
        EXPECT_LE(mid, 1.1f);
    }
}

TEST(TweenTest, TransformPositionOnce) {
    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 0.5f;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = {0.0f, 0.0f, 0.0f}});

    tween_position(world, entity, glm::vec3{10.0f, 0.0f, 0.0f}, 1.0f, Ease::Linear, LoopType::Once);

    EXPECT_NE(world.try_get<TweenTransform>(entity), nullptr);

    // Tick 1: elapsed 0.5s of 1.0s -> position should be 5.0f
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 5.0f, 1e-4f);
    EXPECT_NE(world.try_get<TweenTransform>(entity), nullptr);

    // Tick 2: elapsed 1.0s of 1.0s -> finished, position 10.0f, component removed, event emitted
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 10.0f, 1e-4f);
    EXPECT_EQ(world.try_get<TweenTransform>(entity), nullptr);

    // Verify event
    bool event_received = false;
    for (const auto& ev : engine::ecs::EventReader<TweenCompletedEvent>{
                 world, world.ctx<engine::ecs::EventCursor<TweenCompletedEvent>>()}) {
        if (ev.entity == entity && ev.kind == TweenKind::Transform) {
            event_received = true;
        }
    }
    EXPECT_TRUE(event_received);
}

TEST(TweenTest, TransformChannels) {
    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 1.0f;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{
        .position = {0.0f, 0.0f, 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
    });

    world.emplace<TweenTransform>(entity, TweenTransform{
        .start_position = {0.0f, 0.0f, 0.0f},
        .target_position = {10.0f, 0.0f, 0.0f},
        .start_scale = {1.0f, 1.0f, 1.0f},
        .target_scale = {2.0f, 2.0f, 2.0f},
        .duration = 1.0f,
        .ease = Ease::Linear,
        .loop = LoopType::Once,
        .channels = TransformTweenChannels::Position | TransformTweenChannels::Scale,
        .initialized = true,
    });

    run_tween_transforms(world);

    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 10.0f, 1e-4f);
    EXPECT_NEAR(world.get<engine::Transform>(entity).scale.x, 2.0f, 1e-4f);
}

TEST(TweenTest, TransformLoop) {
    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 0.5f;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = {0.0f, 0.0f, 0.0f}});

    tween_position(world, entity, glm::vec3{10.0f, 0.0f, 0.0f}, 1.0f, Ease::Linear, LoopType::Loop);

    // 0.5s -> 5.0
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 5.0f, 1e-4f);

    // 1.0s -> wraps back to 0.0s -> 0.0
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 0.0f, 1e-4f);
    EXPECT_NE(world.try_get<TweenTransform>(entity), nullptr);

    // 1.5s -> 5.0 again
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 5.0f, 1e-4f);
}

TEST(TweenTest, TransformYoyo) {
    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 0.5f;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = {0.0f, 0.0f, 0.0f}});

    tween_position(world, entity, glm::vec3{10.0f, 0.0f, 0.0f}, 1.0f, Ease::Linear, LoopType::Yoyo);

    // 0.5s: moving forward -> 5.0
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 5.0f, 1e-4f);

    // 1.0s: reaches target (10.0), yoyo direction flips to reverse, wraps to 0.0s in reverse mode -> 10.0
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 10.0f, 1e-4f);
    EXPECT_TRUE(world.get<TweenTransform>(entity).yoyo_direction);

    // 1.5s: 0.5s in reverse -> 5.0
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 5.0f, 1e-4f);

    // 2.0s: reaches start (0.0), yoyo flips back forward -> 0.0
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 0.0f, 1e-4f);
    EXPECT_FALSE(world.get<TweenTransform>(entity).yoyo_direction);
}

TEST(TweenTest, ColorFadeOnce) {
    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 0.25f;

    const engine::ecs::Entity entity = world.create();
    engine::render::Sprite sprite;
    sprite.color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
    world.emplace<engine::render::Sprite>(entity, sprite);

    tween_fade(world, entity, 0.0f, 0.5f, Ease::Linear, LoopType::Once);

    EXPECT_NE(world.try_get<TweenColor>(entity), nullptr);

    // Tick 1: 0.25s / 0.5s -> alpha 0.5
    run_tween_colors(world);
    EXPECT_NEAR(world.get<engine::render::Sprite>(entity).color.a, 0.5f, 1e-4f);

    // Tick 2: 0.5s / 0.5s -> alpha 0.0, finished, removed
    run_tween_colors(world);
    EXPECT_NEAR(world.get<engine::render::Sprite>(entity).color.a, 0.0f, 1e-4f);
    EXPECT_EQ(world.try_get<TweenColor>(entity), nullptr);

    // Verify event
    bool event_received = false;
    for (const auto& ev : engine::ecs::EventReader<TweenCompletedEvent>{
                 world, world.ctx<engine::ecs::EventCursor<TweenCompletedEvent>>()}) {
        if (ev.entity == entity && ev.kind == TweenKind::Color) {
            event_received = true;
        }
    }
    EXPECT_TRUE(event_received);
}

TEST(TweenTest, ReplaceActiveTween) {
    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 0.5f;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = {0.0f, 0.0f, 0.0f}});

    // Start tween to 10
    tween_position(world, entity, glm::vec3{10.0f, 0.0f, 0.0f}, 1.0f, Ease::Linear, LoopType::Once);
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 5.0f, 1e-4f);

    // Retarget to 25 from current (5.0) with duration 2.0s
    tween_position(world, entity, glm::vec3{25.0f, 0.0f, 0.0f}, 2.0f, Ease::Linear, LoopType::Once);
    EXPECT_NEAR(world.get<TweenTransform>(entity).start_position.x, 5.0f, 1e-4f);
    EXPECT_NEAR(world.get<TweenTransform>(entity).target_position.x, 25.0f, 1e-4f);

    // Tick 1.0s: half of 2.0s -> 5.0 + 10.0 = 15.0
    world.ctx<engine::Time>().delta_time = 1.0f;
    run_tween_transforms(world);
    EXPECT_NEAR(world.get<engine::Transform>(entity).position.x, 15.0f, 1e-4f);
}
