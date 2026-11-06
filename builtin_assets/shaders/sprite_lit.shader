<shader lang="slang">
<slang><![CDATA[
// shader_lit with Lambert off unless a normal map is bound. A sprite's +Z normal would zero a light in its plane.

struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(2)]] float3 aNormal : NORMAL;
    [[vk::location(3)]] float4 aColor : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
    [[vk::location(1)]] float3 vNormal : NORMAL;
    [[vk::location(2)]] float4 vColor : COLOR0;
    [[vk::location(3)]] float3 vViewPos : TEXCOORD1;
    [[vk::location(4)]] float3 vWorldPos : TEXCOORD2;
};

uniform float4x4 uModel;
uniform float4x4 uView;
uniform float4x4 uProjection;
uniform float3x3 uNormalMatrix;
uniform float2 uUvScale;
uniform float2 uUvOffset;

uniform Sampler2D uTexture;
uniform float4 uColor;
uniform float uAlphaCutoff;
uniform int uFogEnabled;
uniform float3 uFogColor;
uniform float2 uFogRange;

#include ".forward_light.slang"

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput out;
    out.vUV = input.aUV * uUvScale + uUvOffset;
    out.vNormal = mul(uNormalMatrix, input.aNormal);
    out.vColor = input.aColor;
    float3 worldPos = mul(uModel, float4(input.aPosition, 1.0)).xyz;
    out.vWorldPos = worldPos;
    float4 viewPos = mul(uView, float4(worldPos, 1.0));
    out.vViewPos = viewPos.xyz;
    out.position = mul(uProjection, viewPos);
    return out;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float4 albedo = uTexture.Sample(input.vUV) * uColor * input.vColor;
    if (albedo.a < uAlphaCutoff) {
        discard;
    }
    float3 normal = normalize(input.vNormal);
    bool lambert = false;
    if (uHasNormalMap != 0) {
        normal = perturb_normal(normal, input.vWorldPos, input.vUV, uNormal.Sample(input.vUV).rgb);
        lambert = true;
    }
    float3 color = forward_shade(albedo.rgb, normal, input.vWorldPos, lambert);
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

        out vec2 vUV;
        out vec3 vNormal;
        out vec4 vColor;
        out vec3 vViewPos;
        out vec3 vWorldPos;

        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        uniform mat3 uNormalMatrix;
        uniform vec2 uUvScale;
        uniform vec2 uUvOffset;

        void main() {
            vUV = aUV * uUvScale + uUvOffset;
            vNormal = uNormalMatrix * aNormal;
            vColor = aColor;
            vec3 worldPos = (uModel * vec4(aPosition, 1.0)).xyz;
            vWorldPos = worldPos;
            vec4 viewPos = uView * vec4(worldPos, 1.0);
            vViewPos = viewPos.xyz;
            gl_Position = uProjection * viewPos;
        }
    ]]></vertex>

    <fragment><![CDATA[
        #version 330 core

        out vec4 FragColor;

        in vec2 vUV;
        in vec3 vNormal;
        in vec4 vColor;
        in vec3 vViewPos;
        in vec3 vWorldPos;

        uniform sampler2D uTexture;
        uniform vec4 uColor;
        uniform float uAlphaCutoff;
        uniform int uFogEnabled;
        uniform vec3 uFogColor;
        uniform vec2 uFogRange;

        #include ".forward_light.glsl"

        void main() {
            vec4 albedo = texture(uTexture, vUV) * uColor * vColor;
            if (albedo.a < uAlphaCutoff) {
                discard;
            }
            vec3 normal = normalize(vNormal);
            bool lambert = false;
            if (uHasNormalMap != 0) {
                normal = perturb_normal(normal, vWorldPos, vUV, texture(uNormal, vUV).rgb);
                lambert = true;
            }
            vec3 color = forward_shade(albedo.rgb, normal, vWorldPos, lambert);
            if (uFogEnabled != 0) {
                float fog = clamp((uFogRange.y - length(vViewPos)) / max(uFogRange.y - uFogRange.x, 0.0001), 0.0, 1.0);
                color = mix(uFogColor, color, fog);
            }
            FragColor = vec4(color, albedo.a);
        }
    ]]></fragment>
</glsl>
</shader>
