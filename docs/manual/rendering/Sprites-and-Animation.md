# Sprites & Animation

Wind handles 2D sprites, sprite atlases, and frame-based sprite animations via `engine::render::Sprite` and `engine::render::SpriteAnimator`.

---

## 1. The `Sprite` Component

A `Sprite` defines the texture asset, quad dimensions, color tint, and UV coordinate region:

```cpp
#include <engine/render/sprite.h>
#include <engine/render/renderable.h>
#include <engine/ecs/transform.h>

// Generated asset IDs from asset_codegen
#include <asset_ids.h>

auto entity = world.create();
world.emplace<engine::ecs::Transform>(entity, glm::vec2{100.0f, 100.0f});

// Spawn a simple textured sprite
world.emplace<engine::render::Sprite>(entity, engine::render::Sprite{
    .texture = assets::textures::hero, // AssetId
    .size = {64.0f, 64.0f},
    .color = {1.0f, 1.0f, 1.0f, 1.0f},
    .uv_offset = {0.0f, 0.0f},
    .uv_scale = {1.0f, 1.0f}
});

// Attach a Renderable tag to include it in the draw sort queue
world.emplace<engine::render::Renderable>(entity, engine::render::Renderable{
    .layer = 0,
    .order_in_layer = 10
});
```

---

## 2. Sprite Sheets and Atlases

When importing multiple sub-sprites packed into one texture (via `layout = "multiple"` in the texture `.meta`), you can retrieve sub-sprites by name using `AssetsDb::get_sprite`:

```cpp
// Fetches the sub-sprite region defined in textures/characters.png.meta
engine::render::Sprite knight = services.assets.get_sprite(
    assets::textures::characters,
    "knight_idle"
);
```
`AssetsDb` computes the normalized UV tiling and offset automatically (correctly flipping vertical coordinates against the OpenGL texture bottom-left origin).

---

## 3. Frame Animation

For 2D flipbook animations, use `engine::render::SpriteAnimator` and animation clip assets:

```cpp
#include <engine/render/animation.h>

// Define or load an animation clip
engine::render::AnimationClip walk_clip{
    .frames = {
        {.uv_offset = {0.0f, 0.0f}, .duration = 0.15f},
        {.uv_offset = {0.25f, 0.0f}, .duration = 0.15f},
        {.uv_offset = {0.50f, 0.0f}, .duration = 0.15f},
        {.uv_offset = {0.75f, 0.0f}, .duration = 0.15f},
    },
    .loop = true
};

world.emplace<engine::render::SpriteAnimator>(entity, engine::render::SpriteAnimator{
    .clip = walk_clip,
    .speed = 1.0f
});
```

In your `Schedule::Frame` update phase, the animation system evaluates elapsed time and updates the entity's `Sprite::uv_offset` and `Sprite::uv_scale`.

---

## Next Steps

- Customizing visuals with shaders in [Materials & Shaders](Materials-and-Shaders.md).
- Understanding draw orders in [Command Buffer & Sorting](Command-Buffer-and-Sort.md).
