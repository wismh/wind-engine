<shader lang="slang">
<slang><![CDATA[
struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
};

uniform float4x4 uModel;
uniform float4x4 uView;
uniform float4x4 uProjection;

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput o;
    o.position = mul(uProjection, mul(uView, mul(uModel, float4(input.aPosition, 1.0))));
    return o;
}

[[vk::binding(1, 0)]]
uniform Sampler2D uTexture;
[[vk::binding(2, 0)]]
uniform Sampler2D uDepth;
uniform float4x4 uInvModel;
uniform float4x4 uInvViewProj;
uniform float4 uColor;
uniform float2 uUvScale;
uniform float2 uUvOffset;
uniform float2 uResolution;
uniform float3 uNdcScale;
uniform float3 uNdcBias;
uniform float3 uDecalAxis;
uniform float uAngleCutoff;
uniform float uNormalFade;
uniform int uHasDepth;

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    if (uHasDepth == 0 || uResolution.x <= 0.0 || uResolution.y <= 0.0) {
        discard;
    }
    float2 pixel = input.position.xy;
    float depth = uDepth.Sample(pixel / uResolution).r;
    float3 ndc = float3(pixel * uNdcScale.xy + uNdcBias.xy, depth * uNdcScale.z + uNdcBias.z);
    float4 world_h = mul(uInvViewProj, float4(ndc, 1.0));
    float3 world = world_h.xyz / world_h.w;
    float4 local_h = mul(uInvModel, float4(world, 1.0));
    float3 local = local_h.xyz / local_h.w;
    if (any(abs(local) > 0.5)) {
        discard;
    }
    float3 n = cross(ddx(world), ddy(world));
    float nlen = length(n);
    n = nlen > 1e-8 ? n / nlen : uDecalAxis;
    float fade = 1.0;
    if (uAngleCutoff > 0.0) {
        float edge = uAngleCutoff + max(uNormalFade, 0.0);
        fade = saturate((abs(dot(n, uDecalAxis)) - uAngleCutoff) / max(edge - uAngleCutoff, 1e-4));
    }
    float2 uv = (local.xy + 0.5) * uUvScale + uUvOffset;
    float4 color = uTexture.Sample(uv) * uColor;
    color.a *= fade;
    if (color.a <= 0.0) {
        discard;
    }
    return color;
}
]]></slang>
<glsl>
<vertex><![CDATA[
#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
]]></vertex>
<fragment><![CDATA[
#version 330 core
out vec4 FragColor;
uniform sampler2D uTexture;
uniform sampler2D uDepth;
uniform mat4 uInvModel;
uniform mat4 uInvViewProj;
uniform vec4 uColor;
uniform vec2 uUvScale;
uniform vec2 uUvOffset;
uniform vec2 uResolution;
uniform vec3 uNdcScale;
uniform vec3 uNdcBias;
uniform vec3 uDecalAxis;
uniform float uAngleCutoff;
uniform float uNormalFade;
uniform int uHasDepth;
void main() {
    if (uHasDepth == 0 || uResolution.x <= 0.0 || uResolution.y <= 0.0) {
        discard;
    }
    float depth = texture(uDepth, gl_FragCoord.xy / uResolution).r;
    vec3 ndc = vec3(gl_FragCoord.xy * uNdcScale.xy + uNdcBias.xy, depth * uNdcScale.z + uNdcBias.z);
    vec4 world_h = uInvViewProj * vec4(ndc, 1.0);
    vec3 world = world_h.xyz / world_h.w;
    vec4 local_h = uInvModel * vec4(world, 1.0);
    vec3 local = local_h.xyz / local_h.w;
    if (any(greaterThan(abs(local), vec3(0.5)))) {
        discard;
    }
    vec3 n = cross(dFdx(world), dFdy(world));
    float nlen = length(n);
    n = nlen > 1e-8 ? n / nlen : uDecalAxis;
    float fade = 1.0;
    if (uAngleCutoff > 0.0) {
        float edge = uAngleCutoff + max(uNormalFade, 0.0);
        fade = clamp((abs(dot(n, uDecalAxis)) - uAngleCutoff) / max(edge - uAngleCutoff, 1e-4), 0.0, 1.0);
    }
    vec2 uv = (local.xy + 0.5) * uUvScale + uUvOffset;
    vec4 color = texture(uTexture, uv) * uColor;
    color.a *= fade;
    if (color.a <= 0.0) {
        discard;
    }
    FragColor = color;
}
]]></fragment>
</glsl>
</shader>
