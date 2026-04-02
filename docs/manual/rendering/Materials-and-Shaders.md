# Materials & Shaders

Wind provides an XML-based shader file and a TOML material file. A material pairs a shader with a texture, a color, and a blend mode. Extra shader parameters are set per draw with `MaterialOverride`.

---

## 1. Authoring Shaders (`.shader`)

Shader source files are formatted as XML: a root element (its name is not checked) with one `<vertex>` and one `<fragment>` child.

> [!IMPORTANT]
> **Wrap GLSL source in `<![CDATA[...]]>`!**
> Because shader files are parsed as XML, characters like `<` or `>` in your GLSL code break the parser, and the asset fails to load as `AssetError::Corrupt`. Keep no whitespace between the tag and `<![CDATA[`.

Write the source for desktop OpenGL (`#version 330 core`); the engine rewrites it for GLES 3.0 on Web and Android.

For a mesh draw, the engine sets these uniforms on your shader and feeds this vertex layout, so declare the ones you use under exactly these names:

| Name | Type | Value |
| --- | --- | --- |
| `aPosition` (location 0) | `vec3` | vertex position |
| `aUV` (location 1) | `vec2` | vertex UV |
| `uModel`, `uView`, `uProjection` | `mat4` | model matrix from `Transform`, camera view, camera projection |
| `uColor` | `vec4` | material color times the instance color |
| `uUvScale`, `uUvOffset` | `vec2` | `Sprite::tiling` / `offset` (`1,1` / `0,0` for a `Renderable`) |
| `uTexture` | `sampler2D` | the material's albedo (texture unit 0) |

Example `assets/shaders/custom_glow.shader`:

```xml
<shader>
    <vertex><![CDATA[
        #version 330 core
        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec2 aUV;

        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        uniform vec2 uUvScale;
        uniform vec2 uUvOffset;

        out vec2 vUV;

        void main() {
            vUV = aUV * uUvScale + uUvOffset;
            gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
        }
    ]]></vertex>

    <fragment><![CDATA[
        #version 330 core
        in vec2 vUV;
        out vec4 FragColor;

        uniform sampler2D uTexture;
        uniform vec4 uColor;
        uniform vec4 uGlowColor;   // yours, set with MaterialOverride
        uniform vec4 uParams;      // yours: x intensity, y time

        void main() {
            vec4 col = texture(uTexture, vUV) * uColor;
            float pulse = 0.5 + 0.5 * sin(uParams.y * 3.0);
            FragColor = col + uGlowColor * (uParams.x * pulse);
        }
    ]]></fragment>
</shader>
```

The engine's builtin `shaders/unlit.shader` is the shortest working example (in an SDK it is `bin/assets/engine/shaders/unlit.shader`).

---

## 2. Authoring Materials (`.mat`)

A material pairs a shader with an optional albedo texture, a color, a blend mode, and depth and culling settings. The shader and texture are written as the asset's 32-character `guid` from its `.meta`:

```toml
# assets/materials/glow.mat
shader = "a1b2c3d4e5f60718293a4b5c6d7e8f90"   # guid of assets/shaders/custom_glow.shader
blend = "alpha"                                  # "opaque" (default), "alpha" or "additive"
color = [1.0, 1.0, 1.0, 1.0]                     # default white
depth_test = false                               # default false
depth_write = false                              # default false
cull = "none"                                    # "none" (default), "back" or "front"

[textures]
albedo = "0123456789abcdef0123456789abcdef"      # guid of a texture; omit for none
```

The defaults are the 2D state: no depth test, no depth write, no culling, so sprites draw in layer order. For a solid 3D mesh write `blend = "opaque"`, `depth = true` (sets `depth_test` and `depth_write` together; an explicit `depth_test` or `depth_write` wins over it) and `cull = "back"`; counter-clockwise triangles are front faces. Glass or another see-through 3D surface is `blend = "alpha"` with `depth_test = true` and `depth_write = false`. Particles ignore depth and culling. In C++ the same settings are `engine::render::RenderState`, the fourth argument of `engine::render::Material`. An unknown `blend` or `cull` value, or a `depth*` key that is not `true` or `false`, makes the material fail to load.

A `shader` or `albedo` that is not 32 lowercase hex characters makes the material fail to load (`AssetError::Corrupt`). Codegen does not rewrite these strings, so copy the GUIDs from the `.meta` files. There is no `[uniforms]` table: custom shader parameters are set per draw (next section). The `.mat` needs its own `.meta` with `importer = "material"`.

Load a material with `assets.get<engine::render::IMaterial>(assets::materials::glow)`.

---

## 3. Dynamic Material Overrides

To change a texture or shader parameters per instance or per frame without creating another material, set `material_override` on a `Renderable` or `Sprite`, using `engine::render::MaterialOverride`:

```cpp
#include <engine/render/material.h>

engine::render::MaterialOverride overrides;
overrides.set_vec4("uParams", glm::vec4{intensity, current_time, 0.0f, 0.0f});
overrides.set_vec4("uGlowColor", glm::vec4{0.2f, 0.9f, 1.0f, 1.0f});
// overrides.albedo = other_texture;   // optionally replaces texture slot 0

world.get<engine::render::Renderable>(entity).material_override = overrides;
```

`set_vec4` replaces the value when the name is already there and appends it otherwise. The override applies to that entity's draw only.

For one parameter, `Sprite` and `Renderable` have a shorthand that creates the override for you:

```cpp
// A hit flash: set it once when the hit happens...
world.get<engine::render::Sprite>(enemy).set_param("uFlash", glm::vec4{1.0f, 1.0f, 1.0f, 1.0f});
// ...and again when it ends (or call clear_overrides() to drop every per-sprite override).
world.get<engine::render::Sprite>(enemy).set_param("uFlash", glm::vec4{0.0f});
```

The value stays on the component until you change it, so no system has to rewrite it every frame, and two enemies that share one material each keep their own value: the override is a component field, not a field of the material. Use one shared `.mat` for all of them and flash only the enemy that was hit. The engine restores the shader uniform after every overridden draw, so the next sprite that uses the same shader and never set the parameter draws with the shader's own value (zero unless the shader sets it another way).

> [!NOTE]
> `MaterialOverride` carries `glm::vec4` values only. Pack scalar parameters (such as radii, flags, or time) into vector components (e.g., `uParams.x`, `uParams.y`), and keep array sizes the shader needs as a constant shared by C++ and GLSL.

---

## Next Steps

- See how draws are queued and ordered in [Command Buffer & Sorting](Command-Buffer-and-Sort.md).
- Control camera projections in [Camera](Camera.md).
