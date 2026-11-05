<shader lang="slang">
<slang><![CDATA[
#define kMaxSkinJoints 128

struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(2)]] float3 aNormal : NORMAL;
    [[vk::location(3)]] float4 aColor : COLOR0;
    [[vk::location(5)]] uint4 aJoints : JOINTS;
    [[vk::location(6)]] float4 aWeights : WEIGHTS;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
    [[vk::location(1)]] float4 vColor : COLOR0;
    [[vk::location(2)]] float3 vViewPos : TEXCOORD1;
};

uniform float4x4 uModel;
uniform float4x4 uView;
uniform float4x4 uProjection;
uniform float2 uUvScale;
uniform float2 uUvOffset;
uniform float4x4 uBones[kMaxSkinJoints];

uniform Sampler2D uTexture;
uniform float4 uColor;
uniform float uAlphaCutoff;
uniform int uFogEnabled;
uniform float3 uFogColor;
uniform float2 uFogRange;

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput out;
    float4x4 skin = input.aWeights[0] * uBones[input.aJoints[0]]
            + input.aWeights[1] * uBones[input.aJoints[1]]
            + input.aWeights[2] * uBones[input.aJoints[2]]
            + input.aWeights[3] * uBones[input.aJoints[3]];
    float3 position = mul(skin, float4(input.aPosition, 1.0)).xyz;
    out.vUV = input.aUV * uUvScale + uUvOffset;
    out.vColor = input.aColor;
    float4 viewPos = mul(uView, mul(uModel, float4(position, 1.0)));
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
    float3 color = albedo.rgb;
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
        layout(location = 5) in uvec4 aJoints;
        layout(location = 6) in vec4 aWeights;

        out vec2 vUV;
        out vec4 vColor;
        out vec3 vViewPos;

        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        uniform vec2 uUvScale;
        uniform vec2 uUvOffset;

        layout(std140, binding = 0) uniform Bones {
            mat4 uBones[128];
        };

        void main() {
            mat4 skin = aWeights[0] * uBones[aJoints[0]]
                    + aWeights[1] * uBones[aJoints[1]]
                    + aWeights[2] * uBones[aJoints[2]]
                    + aWeights[3] * uBones[aJoints[3]];
            vec3 position = (skin * vec4(aPosition, 1.0)).xyz;
            vUV = aUV * uUvScale + uUvOffset;
            vColor = aColor;
            vec4 viewPos = uView * uModel * vec4(position, 1.0);
            vViewPos = viewPos.xyz;
            gl_Position = uProjection * viewPos;
        }
    ]]></vertex>

    <fragment><![CDATA[
        #version 330 core

        out vec4 FragColor;

        in vec2 vUV;
        in vec4 vColor;
        in vec3 vViewPos;

        uniform sampler2D uTexture;
        uniform vec4 uColor;
        uniform float uAlphaCutoff;
        uniform int uFogEnabled;
        uniform vec3 uFogColor;
        uniform vec2 uFogRange;

        void main() {
            vec4 albedo = texture(uTexture, vUV) * uColor * vColor;
            if (albedo.a < uAlphaCutoff) {
                discard;
            }
            vec3 color = albedo.rgb;
            if (uFogEnabled != 0) {
                float fog = clamp((uFogRange.y - length(vViewPos)) / max(uFogRange.y - uFogRange.x, 0.0001), 0.0, 1.0);
                color = mix(uFogColor, color, fog);
            }
            FragColor = vec4(color, albedo.a);
        }
    ]]></fragment>
</glsl>
</shader>
