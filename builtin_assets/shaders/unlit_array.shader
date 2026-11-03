<shader lang="slang">
<slang><![CDATA[
// builtin::shader_unlit sampling a texture array: uTexture is a Sampler2DArray, the layer is MeshVertex::layer.
// No animated tiles (location 2 is not read).
struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(3)]] float4 aColor : COLOR0;
    [[vk::location(4)]] float aLayer : TEXCOORD1;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
    [[vk::location(1)]] float4 vColor : COLOR0;
    [[vk::location(2)]] float3 vViewPos : TEXCOORD1;
    [[vk::location(3)]] nointerpolation float vLayer : TEXCOORD2;
};

uniform float4x4 uModel;
uniform float4x4 uView;
uniform float4x4 uProjection;
uniform float2 uUvScale;
uniform float2 uUvOffset;
uniform Sampler2DArray uTexture;
uniform float4 uColor;
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
    out.vColor = input.aColor;
    out.vLayer = input.aLayer;
    float4 viewPos = mul(uView, mul(uModel, float4(input.aPosition, 1.0)));
    out.vViewPos = viewPos.xyz;
    out.position = mul(uProjection, viewPos);
    return out;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float4 color = uTexture.Sample(float3(input.vUV, input.vLayer)) * uColor * input.vColor;
    if (color.a < uAlphaCutoff) {
        discard;
    }
    if (uFogEnabled != 0) {
        float fog = clamp((uFogRange.y - length(input.vViewPos)) / max(uFogRange.y - uFogRange.x, 0.0001), 0.0, 1.0);
        color.rgb = lerp(uFogColor, color.rgb, fog);
    }
    return color;
}
]]></slang>
<glsl>
    <vertex><![CDATA[
        #version 330 core

        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec2 aUV;
        layout(location = 3) in vec4 aColor;
        layout(location = 4) in float aLayer;

        out vec2 vUV;
        out vec4 vColor;
        out vec3 vViewPos;
        flat out float vLayer;

        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        uniform vec2 uUvScale;
        uniform vec2 uUvOffset;

        void main() {
            vUV = aUV * uUvScale + uUvOffset;
            vColor = aColor;
            vLayer = aLayer;
            vec4 viewPos = uView * uModel * vec4(aPosition, 1.0);
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
        flat in float vLayer;
        uniform sampler2DArray uTexture;
        uniform vec4 uColor;
        // RenderState::alpha_cutoff, 0 when unset (alpha is never below 0).
        uniform float uAlphaCutoff;
        // CmdSetView::fog: enabled flag, color, and (start, end).
        uniform int uFogEnabled;
        uniform vec3 uFogColor;
        uniform vec2 uFogRange;

        void main() {
            vec4 color = texture(uTexture, vec3(vUV, vLayer)) * uColor * vColor;
            if (color.a < uAlphaCutoff) {
                discard;
            }
            if (uFogEnabled != 0) {
                float fog = clamp((uFogRange.y - length(vViewPos)) / max(uFogRange.y - uFogRange.x, 0.0001), 0.0, 1.0);
                color.rgb = mix(uFogColor, color.rgb, fog);
            }
            FragColor = color;
        }
    ]]></fragment>
</glsl>
</shader>
