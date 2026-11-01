<shader lang="slang"><![CDATA[
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
    for (int i = 0; i < kMaxDirectionalLights; ++i) {
        if (i >= uLightCount) {
            break;
        }
        light += uLightColors[i] * max(dot(normal, -uLightDirections[i]), 0.0);
    }
    return float4(albedo.rgb * light, albedo.a);
}
]]></shader>
