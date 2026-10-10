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

A material pairs a shader with an optional albedo texture, a color, and a blend mode. The shader and texture are written as the asset's 32-character `guid` from its `.meta`:

```toml
# assets/materials/glow.mat
shader = "a1b2c3d4e5f60718293a4b5c6d7e8f90"   # guid of assets/shaders/custom_glow.shader
blend = "alpha"                                  # "opaque" (default), "alpha" or "additive"
color = [1.0, 1.0, 1.0, 1.0]                     # default white

[textures]
albedo = "0123456789abcdef0123456789abcdef"      # guid of a texture; omit for none
```

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

> [!NOTE]
> `MaterialOverride` carries `glm::vec4` values only. Pack scalar parameters (such as radii, flags, or time) into vector components (e.g., `uParams.x`, `uParams.y`), and keep array sizes the shader needs as a constant shared by C++ and GLSL.

---

## Next Steps

- See how draws are queued and ordered in [Command Buffer & Sorting](Command-Buffer-and-Sort.md).
- Control camera projections in [Camera](Camera.md).
