<shader lang="slang">
<slang><![CDATA[
struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(2)]] float3 aAnim : NORMAL;
    [[vk::location(3)]] float4 aColor : COLOR0;
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
uniform float uTime;
uniform Sampler2D uTexture;
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
    float2 uv = input.aUV;
    if (input.aAnim.x > 1.0) {
        float scaled = uTime * input.aAnim.y;
        float frame = floor(scaled - input.aAnim.x * floor(scaled / input.aAnim.x));
        uv.x += frame * input.aAnim.z;
    }
    out.vUV = uv * uUvScale + uUvOffset;
    out.vColor = input.aColor;
    float4 viewPos = mul(uView, mul(uModel, float4(input.aPosition, 1.0)));
    out.vViewPos = viewPos.xyz;
    out.position = mul(uProjection, viewPos);
    return out;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float4 color = uTexture.Sample(input.vUV) * uColor * input.vColor;
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
        layout(location = 2) in vec3 aAnim;
        layout(location = 3) in vec4 aColor;

        out vec2 vUV;
        out vec4 vColor;
        out vec3 vViewPos;

        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        uniform vec2 uUvScale;
        uniform vec2 uUvOffset;
        uniform float uTime;

        void main() {
            vec2 uv = aUV;
            if (aAnim.x > 1.0) {
                float frame = floor(mod(uTime * aAnim.y, aAnim.x));
                uv.x += frame * aAnim.z;
            }
            vUV = uv * uUvScale + uUvOffset;
            vColor = aColor;
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
        uniform sampler2D uTexture;
        uniform vec4 uColor;
        // RenderState::alpha_cutoff, 0 when unset (alpha is never below 0).
        uniform float uAlphaCutoff;
        // CmdSetView::fog: enabled flag, color, and (start, end).
        uniform int uFogEnabled;
        uniform vec3 uFogColor;
        uniform vec2 uFogRange;

        void main() {
            vec4 color = texture(uTexture, vUV) * uColor * vColor;
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
