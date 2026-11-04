<shader lang="slang">
<slang><![CDATA[
// builtin::shader_lit sampling a texture array: uTexture is a Sampler2DArray, the layer is MeshVertex::layer.
// Same number as engine::render::kMaxDirectionalLights (include/engine/render/scene_lights.h).
#define kMaxDirectionalLights 4

struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(2)]] float3 aNormal : NORMAL;
    [[vk::location(3)]] float4 aColor : COLOR0;
    [[vk::location(4)]] float aLayer : TEXCOORD1;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
    [[vk::location(1)]] float3 vNormal : NORMAL;
    [[vk::location(2)]] float4 vColor : COLOR0;
    [[vk::location(3)]] float3 vViewPos : TEXCOORD1;
    [[vk::location(4)]] nointerpolation float vLayer : TEXCOORD2;
};

uniform float4x4 uModel;
uniform float4x4 uView;
uniform float4x4 uProjection;
// Inverse transpose of mat3(uModel), set by the backend for every mesh draw.
uniform float3x3 uNormalMatrix;
uniform float2 uUvScale;
uniform float2 uUvOffset;

uniform Sampler2DArray uTexture;
uniform float4 uColor;
uniform int uLightCount;
// World-space directions the lights travel along (unit length), and color * intensity.
uniform float3 uLightDirections[kMaxDirectionalLights];
uniform float3 uLightColors[kMaxDirectionalLights];
uniform float3 uAmbient;
// RenderState::alpha_cutoff, 0 when unset (alpha is never below 0).
uniform float uAlphaCutoff;
// CmdSetView::fog: enabled flag, color, and (start, end).
uniform int uFogEnabled;
uniform float3 uFogColor;
uniform float2 uFogRange;

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput out;
    out.vUV = input.aUV * uUvScale + uUvOffset;
    out.vNormal = mul(uNormalMatrix, input.aNormal);
    out.vColor = input.aColor;
    out.vLayer = input.aLayer;
    float4 viewPos = mul(uView, mul(uModel, float4(input.aPosition, 1.0)));
    out.vViewPos = viewPos.xyz;
    out.position = mul(uProjection, viewPos);
    return out;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float4 albedo = uTexture.Sample(float3(input.vUV, input.vLayer)) * uColor * input.vColor;
    if (albedo.a < uAlphaCutoff) {
        discard;
    }
    float3 normal = normalize(input.vNormal);
    float3 light = uAmbient;
    for (int i = 0; i < 4; ++i) {
        if (i >= uLightCount) {
            break;
        }
        light += uLightColors[i] * max(dot(normal, -uLightDirections[i]), 0.0);
    }
    float3 color = albedo.rgb * light;
    if (uFogEnabled != 0) {
        float fog = clamp((uFogRange.y - length(input.vViewPos)) / max(uFogRange.y - uFogRange.x, 0.0001), 0.0, 1.0);
        color = lerp(uFogColor, color, fog);
    }
    return float4(color, albedo.a);
}
]]></slang>
<glsl>
    <vertex><![CDATA[
        #version 330 core

        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec2 aUV;
        layout(location = 2) in vec3 aNormal;
        layout(location = 3) in vec4 aColor;
        layout(location = 4) in float aLayer;

        out vec2 vUV;
        out vec3 vNormal;
        out vec4 vColor;
        out vec3 vViewPos;
        flat out float vLayer;

        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        // Inverse transpose of mat3(uModel), set by the backend for every mesh draw.
        uniform mat3 uNormalMatrix;
        uniform vec2 uUvScale;
        uniform vec2 uUvOffset;

        void main() {
            vUV = aUV * uUvScale + uUvOffset;
            vNormal = uNormalMatrix * aNormal;
            vColor = aColor;
            vLayer = aLayer;
            vec4 viewPos = uView * uModel * vec4(aPosition, 1.0);
            vViewPos = viewPos.xyz;
            gl_Position = uProjection * viewPos;
        }
    ]]></vertex>

    <fragment><![CDATA[
        #version 330 core

        // Same number as engine::render::kMaxDirectionalLights (include/engine/render/scene_lights.h).
        #define kMaxDirectionalLights 4

        out vec4 FragColor;

        in vec2 vUV;
        in vec3 vNormal;
        in vec4 vColor;
        in vec3 vViewPos;
        flat in float vLayer;

        uniform sampler2DArray uTexture;
        uniform vec4 uColor;
        uniform int uLightCount;
        // World-space directions the lights travel along (unit length), and color * intensity.
        uniform vec3 uLightDirections[4];
        uniform vec3 uLightColors[4];
        uniform vec3 uAmbient;
        // RenderState::alpha_cutoff, 0 when unset (alpha is never below 0).
        uniform float uAlphaCutoff;
        // CmdSetView::fog: enabled flag, color, and (start, end).
        uniform int uFogEnabled;
        uniform vec3 uFogColor;
        uniform vec2 uFogRange;

        void main() {
            vec4 albedo = texture(uTexture, vec3(vUV, vLayer)) * uColor * vColor;
            if (albedo.a < uAlphaCutoff) {
                discard;
            }
            vec3 normal = normalize(vNormal);
            vec3 light = uAmbient;
            for (int i = 0; i < 4; ++i) {
                if (i >= uLightCount) {
                    break;
                }
                light += uLightColors[i] * max(dot(normal, -uLightDirections[i]), 0.0);
            }
            vec3 color = albedo.rgb * light;
            if (uFogEnabled != 0) {
                float fog = clamp((uFogRange.y - length(vViewPos)) / max(uFogRange.y - uFogRange.x, 0.0001), 0.0, 1.0);
                color = mix(uFogColor, color, fog);
            }
            FragColor = vec4(color, albedo.a);
        }
    ]]></fragment>
</glsl>
</shader>
