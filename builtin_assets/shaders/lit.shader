<shader lang="slang">
<slang><![CDATA[
// Same number as engine::render::kMaxDirectionalLights (include/engine/render/scene_lights.h).
#define kMaxDirectionalLights 4

struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(2)]] float3 aNormal : NORMAL;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
    [[vk::location(1)]] float3 vNormal : NORMAL;
};

uniform float4x4 uModel;
uniform float4x4 uView;
uniform float4x4 uProjection;
// Inverse transpose of mat3(uModel), set by the backend for every mesh draw.
uniform float3x3 uNormalMatrix;
uniform float2 uUvScale;
uniform float2 uUvOffset;

uniform Sampler2D uTexture;
uniform float4 uColor;
uniform int uLightCount;
// World-space directions the lights travel along (unit length), and color * intensity.
uniform float3 uLightDirections[kMaxDirectionalLights];
uniform float3 uLightColors[kMaxDirectionalLights];
uniform float3 uAmbient;

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput out;
    out.vUV = input.aUV * uUvScale + uUvOffset;
    out.vNormal = mul(uNormalMatrix, input.aNormal);
    out.position = mul(uProjection, mul(uView, mul(uModel, float4(input.aPosition, 1.0))));
    return out;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float4 albedo = uTexture.Sample(input.vUV) * uColor;
    float3 normal = normalize(input.vNormal);
    float3 light = uAmbient;
    for (int i = 0; i < 4; ++i) {
        if (i >= uLightCount) {
            break;
        }
        light += uLightColors[i] * max(dot(normal, -uLightDirections[i]), 0.0);
    }
    return float4(albedo.rgb * light, albedo.a);
}
]]></slang>
<glsl>
    <vertex><![CDATA[
        #version 330 core

        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec2 aUV;
        layout(location = 2) in vec3 aNormal;

        out vec2 vUV;
        out vec3 vNormal;

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
            gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
        }
    ]]></vertex>

    <fragment><![CDATA[
        #version 330 core

        // Same number as engine::render::kMaxDirectionalLights (include/engine/render/scene_lights.h).
        #define kMaxDirectionalLights 4

        out vec4 FragColor;

        in vec2 vUV;
        in vec3 vNormal;

        uniform sampler2D uTexture;
        uniform vec4 uColor;
        uniform int uLightCount;
        // World-space directions the lights travel along (unit length), and color * intensity.
        uniform vec3 uLightDirections[4];
        uniform vec3 uLightColors[4];
        uniform vec3 uAmbient;

        void main() {
            vec4 albedo = texture(uTexture, vUV) * uColor;
            vec3 normal = normalize(vNormal);
            vec3 light = uAmbient;
            for (int i = 0; i < 4; ++i) {
                if (i >= uLightCount) {
                    break;
                }
                light += uLightColors[i] * max(dot(normal, -uLightDirections[i]), 0.0);
            }
            FragColor = vec4(albedo.rgb * light, albedo.a);
        }
    ]]></fragment>
</glsl>
</shader>
