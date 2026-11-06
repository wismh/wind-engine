# Basic 3D

Wind is 2D-first, but it can draw simple lit 3D scenes: a perspective camera, meshes with normals, a builtin lit material, and directional and ambient lights. A 2D game does not need any of this and draws exactly as it did.

---

## 1. A Lit Cube

```cpp
#include <engine/builtin_ids.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/transform.h>
#include <engine/render/ambient_light.h>
#include <engine/render/directional_light.h>
#include <engine/render/primitives.h>
#include <engine/render/renderable.h>

#include <glm/gtc/quaternion.hpp>

// Camera: perspective, 6 units in front of the origin, looking down -Z.
engine::ecs::Entity cam = world.create();
engine::Transform cam_transform;
cam_transform.position = {0.0f, 1.5f, 6.0f};
world.emplace<engine::Transform>(cam, cam_transform);
world.emplace<engine::Camera>(cam, engine::Camera{
        .projection = engine::Projection::Perspective,
        .fov_y = glm::radians(60.0f),
        .near_clip = 0.1f,
        .far_clip = 100.0f,
});
world.ctx<engine::ActiveCamera>().entity = cam;

// Cube: a mesh built once in code, drawn with the builtin lit material.
engine::ecs::Entity cube = world.create();
engine::Transform cube_transform;
cube_transform.rotation = engine::euler_xyz({0.3f, 0.6f, 0.0f});
world.emplace<engine::Transform>(cube, cube_transform);
world.emplace<engine::render::Renderable>(cube, engine::render::Renderable{
        .mesh = services.graphics.create_mesh(engine::render::make_cube()),
        .material = services.assets.get<engine::render::IMaterial>(engine::builtin::material_lit),
        .color = {0.9f, 0.4f, 0.2f, 1.0f},   // tint: the material is white
});

// Sun: aimed by its rotation. It shines along its local -Z.
engine::ecs::Entity sun = world.create();
engine::Transform sun_transform;
sun_transform.rotation = engine::euler_xyz({glm::radians(-50.0f), glm::radians(30.0f), 0.0f});
world.emplace<engine::Transform>(sun, sun_transform);
world.emplace<engine::render::DirectionalLight>(sun, engine::render::DirectionalLight{.intensity = 1.0f});

// Fill light everywhere.
world.emplace<engine::render::AmbientLight>(world.create(), engine::render::AmbientLight{.intensity = 0.25f});
```

`engine::builtin::mesh_cube` is the same cube as an asset, if you prefer `services.assets.get<engine::render::IMesh>(engine::builtin::mesh_cube)`. `make_plane` and `make_uv_sphere` give a floor and a ball.

`builtin::material_lit` is white, untextured, opaque, depth-tested, and culls back faces. Make your own lit material by pointing a `.mat` at the lit shader's GUID (`a0e1b2c3d4f5678901234567890abc0a`) with an albedo texture, `blend = "opaque"`, `depth = true`, and `cull = "back"` ([Materials & Shaders](Materials-and-Shaders.md)).

---

## 2. Lights

| Component | Fields | Meaning |
| --- | --- | --- |
| `engine::render::DirectionalLight` | `color` (white), `intensity` (1) | Light from far away, like the sun. It shines along the entity's world -Z, so `Transform::rotation` aims it and a `Parent` carries it. Position and scale do not matter. |
| `engine::render::AmbientLight` | `color` (white), `intensity` (0.2) | Light that reaches every surface equally. All ambient lights add up. |

- Up to 4 directional lights are used (`engine::render::kMaxDirectionalLights`), the ones on the lowest entity indices. More log a warning once and are ignored.
- A world with no light entities at all gets a default rig: one white light from above, in front, and to the left, plus ambient 0.2. So a lit cube is visible before you add lights. As soon as the world has any `DirectionalLight` or `AmbientLight`, only your lights are used.
- The lit shader is Lambert: each surface gets `ambient + sum(light color * max(0, dot(normal, toward the light)))`, times its color. There are no shadows, point lights, or specular highlights.
- Lights do nothing to unlit materials, so sprites and UI look the same in a scene with lights.

---

## 3. What the Engine Does

Every frame the render system pushes one `CmdSetView` per window (camera view, that window's projection, camera position, and the lights), then the sorted draws. The backend sets the lighting uniforms on every mesh draw; a shader that does not declare them ignores them. A custom lit shader can read the same uniforms (`uNormalMatrix`, `uLightCount`, `uLightColorKind`, `uLightDirRange`, `uLightPosSpot`, `uLightSpotBias`, `uAmbient`, `uCameraPosition`, `uGloss`; see [Materials & Shaders](Materials-and-Shaders.md)). `#include ".forward_light.glsl"` pastes the builtin shade function when that file sits next to the shader.

Opaque depth-tested draws go first, nearest first, so the depth buffer hides what is behind. See [Command Buffer & Sorting](Command-Buffer-and-Sort.md#2-draw-order).

**Frustum culling.** A `Renderable` (or `Sprite`) whose mesh box is outside the camera's view in every window is not drawn at all. The box is `IMesh::bounds()`, taken from the mesh's vertices when it is created and kept current by `mesh->update(desc)`, so a voxel chunk you rebuild needs nothing extra. A particle emitter is culled from its live particles the same way; `emitter.frustum_cull = false` always draws it, and that flag is not a `.world` key. If your shader moves vertices outside the mesh's own box (a waving flag, a sky dome around the camera), set `renderable.frustum_cull = false` (`.world`: `frustum_cull = false`) so it is always drawn. For your own visibility checks (chunk streaming, AI), `engine::camera_frustum(camera, camera_world, window_size)` gives the same frustum and `engine::render::frustum_intersects(frustum, engine::render::transform_aabb(box, model))` the same test.

---

## 4. Cutout and Fog

**Vertex color.** `engine::render::MeshVertex::color` (white by default) multiplies the albedo in the builtin lit and unlit shaders. Bake light and ambient occlusion into it when you build a mesh; primitives and `.mesh` rows without color stay white. A `.mesh` row with twelve numbers (`px py pz nx ny nz u v r g b a`) sets it.

**Cutout.** `alpha_cutoff = 0.5` in a `.mat` (or `RenderState::alpha_cutoff` in C++) discards fragments whose alpha is below 0.5. An opaque, depth-tested cutout material still draws in the opaque pass.

**Fog.** Put an `engine::render::Fog` on the active camera:

```cpp
#include <engine/render/fog.h>

world.emplace<engine::render::Fog>(cam, engine::render::Fog{
        .color = {0.7f, 0.8f, 0.9f},
        .start = 40.0f,   // no fog nearer than this
        .end = 120.0f,    // fully fog colored from here on
});
```

The builtin lit and unlit shaders (so sprites and tilemaps too) fade linearly to `color` between `start` and `end`, by distance from the camera. Card particles, UI, and the editor's scene viewport are not fogged. Mesh particles use those shaders, so a game view with `Fog` fogs them. Without a `Fog` component nothing is fogged. In a `.world` file it is `fog = { color = [0.7, 0.8, 0.9], start = 40.0, end = 120.0 }`.

---

## 5. Block Textures: Texture Arrays and Mipmaps

A block atlas drawn with nearest filtering shimmers at a distance, and mipmapping an atlas bleeds neighbouring tiles into each other. Give each block face its own layer of a texture array instead. Mark the PNG as an array in its `.meta`; every `[[sprites]]` frame becomes one layer:

```toml
guid = "3c4d5e6f708192a3b4c5d6e7f8091a2b"
importer = "texture_array"
filter = "nearest"
mipmaps = "linear"

[[sprites]]
name = "stone"
rect = [0, 0, 16, 16]

[[sprites]]
name = "dirt"
rect = [16, 0, 16, 16]
```

All frames must be the same size and inside the image, at most 256 of them; otherwise the cook fails. Point a material at a builtin array shader (`builtin::shader_lit_array`, GUID `a0e1b2c3d4f5678901234567890abc0c`, or `builtin::shader_unlit_array`, `...abc0d`, when light is baked into vertex colors):

```toml
shader = "asset://a0e1b2c3d4f5678901234567890abc0d"
blend = "opaque"
depth = true
cull = "back"

[textures]
albedo = "asset://3c4d5e6f708192a3b4c5d6e7f8091a2b"
```

Your mesher writes the layer of each face into `MeshVertex::layer`, the same value on every vertex of a triangle:

```cpp
#include <engine/render/texture_array.h>

auto blocks = services.assets.get<engine::render::TextureArray>(assets::textures::blocks);
const float stone = static_cast<float>(*blocks->layer("stone"));
vertex.layer = stone;   // with vertex.uv in 0..1 over the face
```

The array PNG cannot also be used as a sprite sheet; keep a separate texture for 2D icons. `.mesh` files have no layer column (their vertices use layer 0). Any texture can be mipmapped with `mipmaps = "nearest"` or `"linear"` in its `.meta`.

---

## Next Steps

- Camera details and screen rays: [Camera](Camera.md).
- Meshes and draw order: [Command Buffer & Sorting](Command-Buffer-and-Sort.md).
