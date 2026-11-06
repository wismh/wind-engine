<shader lang="slang">
<slang><![CDATA[
struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(2)]] float3 aNormal : NORMAL;
    [[vk::location(3)]] float4 aColor : COLOR0;
    [[vk::location(7)]] float3 aInstancePos : TEXCOORD3;
    [[vk::location(8)]] float3 aInstanceScale : TEXCOORD4;
    [[vk::location(9)]] float4 aInstanceColor : COLOR1;
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
uniform int uInstanced;

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
    float3 worldPos;
    if (uInstanced != 0) {
        worldPos = input.aInstancePos + input.aPosition * input.aInstanceScale;
        out.vColor = input.aColor * input.aInstanceColor;
    } else {
        worldPos = mul(uModel, float4(input.aPosition, 1.0)).xyz;
        out.vColor = input.aColor;
    }
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
    if (uHasNormalMap != 0) {
        normal = perturb_normal(normal, input.vWorldPos, input.vUV, uNormal.Sample(input.vUV).rgb);
    }
    float3 color = forward_shade(albedo.rgb, normal, input.vWorldPos, true);
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
        layout(location = 7) in vec3 aInstancePos;
        layout(location = 8) in vec3 aInstanceScale;
        layout(location = 9) in vec4 aInstanceColor;

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
        uniform int uInstanced;

        void main() {
            vUV = aUV * uUvScale + uUvOffset;
            vNormal = uNormalMatrix * aNormal;
            vec3 worldPos;
            if (uInstanced != 0) {
                worldPos = aInstancePos + aPosition * aInstanceScale;
                vColor = aColor * aInstanceColor;
            } else {
                worldPos = (uModel * vec4(aPosition, 1.0)).xyz;
                vColor = aColor;
            }
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
            if (uHasNormalMap != 0) {
                normal = perturb_normal(normal, vWorldPos, vUV, texture(uNormal, vUV).rgb);
            }
            vec3 color = forward_shade(albedo.rgb, normal, vWorldPos, true);
            if (uFogEnabled != 0) {
                float fog = clamp((uFogRange.y - length(vViewPos)) / max(uFogRange.y - uFogRange.x, 0.0001), 0.0, 1.0);
                color = mix(uFogColor, color, fog);
            }
            FragColor = vec4(color, albedo.a);
        }
    ]]></fragment>
</glsl>
</shader>
