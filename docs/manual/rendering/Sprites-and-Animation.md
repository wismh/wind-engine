# Sprites & Animation

Wind handles 2D sprites, sprite sheets, and frame-based sprite animations via `engine::render::Sprite` and `engine::render::SpriteAnimator`. An entity with a `Sprite` and a `Transform` is drawn by the engine; you do not push draw commands.

---

## 1. The `Sprite` Component

A `Sprite` holds the texture, tint, draw order, flips, UV tiling and offset, and the size and pivot of its quad. The easiest way to fill it is `AssetsDb::get_sprite`, which sets the texture, the pixel size, `pixels_per_unit`, the pivot, and the UV region:

```cpp
#include <engine/ecs/transform.h>
#include <engine/render/sprite.h>
#include <engine/resources/assets_db.h>

// Generated asset IDs from asset_codegen
#include <asset_ids.h>

engine::ecs::Entity entity = world.create();
world.emplace<engine::Transform>(entity, engine::Transform{.position = {1.0f, 1.0f, 0.0f}});

// A whole single-layout texture (the name is empty)
engine::render::Sprite sprite = services.assets.get_sprite(assets::textures::hero);
sprite.layer = 0;
sprite.order_in_layer = 10;
sprite.color = {1.0f, 1.0f, 1.0f, 1.0f};   // tint, multiplied by the material color
world.emplace<engine::render::Sprite>(entity, std::move(sprite));
```

No other component is needed. An entity that also has a `Renderable` draws the `Renderable` and skips the `Sprite`.

How big the sprite is: its size in world units is `pixel_size / pixels_per_unit` (`pixels_per_unit` defaults to 100, or the value in the texture's `.meta`), with the quad's `pivot` (`{0.5, 0.5}` is the center) at the `Transform` position. `Transform::scale` scales it further. If you build a `Sprite` by hand and leave `pixel_size` at `{0, 0}`, the quad is drawn at the mesh's own size and the `Transform` alone controls it.

Other fields: `flip_x`, `flip_y`, `tiling` and `offset` (the UV scale and offset), and the optional `mesh`, `material`, and `material_override`. With no `material`, the engine draws the texture with the builtin unlit shader and alpha blending.

---

## 2. Sprite Sheets and Atlases

When several sub-sprites are packed into one texture, set `layout = "multiple"` in the texture's `.meta` and list the rectangles in pixels from the top-left corner:

```toml
# assets/textures/characters.png.meta
guid = "00112233445566778899aabbccddeeff"
importer = "texture"
layout = "multiple"
pixels_per_unit = 32

[[sprites]]
name = "knight_idle"
rect = { x = 0, y = 0, w = 32, h = 32 }

[[sprites]]
name = "knight_run_0"
rect = { x = 32, y = 0, w = 32, h = 32 }
pivot = { x = 0.5, y = 0.0 }       # optional, per sprite
```

Then fetch a sub-sprite by name with `AssetsDb::get_sprite` (`try_get_sprite` returns an `AssetError` instead of stopping the game):

```cpp
// The region named in textures/characters.png.meta
engine::render::Sprite knight = services.assets.get_sprite(assets::textures::characters, "knight_idle");
```

`AssetsDb` computes the UV `tiling` and `offset` for you (flipping the vertical axis against the OpenGL bottom-left origin). On a `multiple` texture an empty name is `AssetError::NotFound`.

---

## 3. Frame Animation

A flipbook is an `.anim` asset (TOML, `importer = "animation"`) that lists sprites, plus a `SpriteAnimator` component beside the `Sprite`:

```toml
# assets/animations/walk.anim
fps = 8            # default frame duration is 1 / fps
loop = true

[[frames]]
texture = "ffeeddccbbaa99887766554433221100"   # guid of the texture (copy it from its .meta)
sprite = "knight_idle"
[[frames]]
texture = "ffeeddccbbaa99887766554433221100"
sprite = "knight_run_0"
duration = 0.2     # optional, seconds, overrides 1 / fps
```

```cpp
#include <engine/render/animation.h>

auto walk = services.assets.get<engine::render::SpriteAnimationClip>(assets::animations::walk);

engine::render::SpriteAnimator animator;
animator.play(walk);          // sets the clip, rewinds, and starts playing
animator.speed = 1.0f;
world.emplace<engine::render::SpriteAnimator>(entity, std::move(animator));
```

Every frame the engine's animation system (`Schedule::Frame`, `Phase::Game`) advances `elapsed` by `Time::delta_time * speed` and copies the current frame's texture, UV region, pixel size, pixels per unit, and pivot onto the entity's `Sprite`. A clip without `loop` stops on its last frame (`playing` becomes false). `SpriteAnimator` also has `pause()`, `resume()`, and `stop()`, and `play(other_clip)` to switch clips (it does nothing when `other_clip` is already playing, unless `restart` is true).

---

## Next Steps

- Customizing visuals with shaders in [Materials & Shaders](Materials-and-Shaders.md).
- Understanding draw orders in [Command Buffer & Sorting](Command-Buffer-and-Sort.md).
