# Materials & Shaders

Wind provides an XML-based shader descriptor and material system supporting custom shaders, uniform properties, and blend modes.

---

## 1. Authoring Shaders (`.shader`)

Shader source files are formatted as XML.

> [!IMPORTANT]
> **Wrap GLSL source in `<![CDATA[...]]>`!**
> Because shader files are parsed as XML, characters like `<` or `>` in your GLSL code will cause the parser to fail silently (`AssetError::Corrupt`). Keep no whitespace between the tag and `<![CDATA[`.

Example `assets/shaders/custom_glow.shader`:

```xml
<shader name="custom_glow">
    <vertex><![CDATA[
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aUV;

        uniform mat4 uViewProj;
        uniform mat4 uModel;

        out vec2 vUV;

        void main() {
            vUV = aUV;
            gl_Position = uViewProj * uModel * vec4(aPos, 0.0, 1.0);
        }
    ]]></vertex>

    <fragment><![CDATA[
        #version 330 core
        in vec2 vUV;
        out vec4 FragColor;

        uniform sampler2D uTexture;
        uniform vec4 uGlowColor;
        uniform vec4 uParams; // x: intensity, y: time

        void main() {
            vec4 col = texture(uTexture, vUV);
            float pulse = 0.5 + 0.5 * sin(uParams.y * 3.0);
            FragColor = col + uGlowColor * (uParams.x * pulse);
        }
    ]]></fragment>
</shader>
```

---

## 2. Authoring Materials (`.mat`)

A material pairs a shader with default textures and blend parameters:

```toml
# assets/materials/glow.mat
shader = "guid:a1b2c3d4..."
blend = "alpha" # "opaque", "alpha", "additive", "multiply"

[uniforms]
uGlowColor = [1.0, 0.8, 0.2, 1.0]
uParams = [2.5, 0.0, 0.0, 0.0]
```

---

## 3. Dynamic Material Overrides

To modify shader parameters per instance or frame without cloning the material, use `engine::render::MaterialOverride`:

```cpp
#include <engine/render/material.h>

// Override uniform vectors for a specific draw call
engine::render::MaterialOverride overrides;
overrides.set_vec4("uParams", glm::vec4{intensity, current_time, 0.0f, 0.0f});
overrides.set_vec4("uGlowColor", glm::vec4{0.2f, 0.9f, 1.0f, 1.0f});
```

> [!NOTE]
> `MaterialOverride` supports 4-component vectors (`glm::vec4`). Pack scalar parameters (such as radii, flags, or time) into vector components (e.g., `uParams.x`, `uParams.y`).

---

## Next Steps

- Submit draw calls in [Command Buffer & Sorting](Command-Buffer-and-Sort.md).
- Control camera projections in [Camera](Camera.md).
