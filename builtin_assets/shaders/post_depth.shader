<shader lang="slang">
<slang><![CDATA[
struct VOut {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
};

[shader("vertex")]
VOut vertexMain(uint vertexId : SV_VertexID) {
    VOut o;
    float2 pos = float2(float((vertexId << 1) & 2), float(vertexId & 2));
    o.position = float4(pos * 2.0 - 1.0, 0.0, 1.0);
    o.vUV = float2(pos.x, pos.y);
    return o;
}

[[vk::binding(1, 0)]]
uniform Sampler2D uScene;
[[vk::binding(2, 0)]]
uniform Sampler2D uDepth;
uniform float4 uParams;

[shader("fragment")]
float4 fragmentMain(VOut input) : SV_Target {
    float4 c = uScene.Sample(input.vUV);
    float depth = uDepth.Sample(input.vUV).r;
    float3 tint = lerp(float3(0.15, 0.25, 0.55), float3(1.0, 0.9, 0.7), depth);
    float strength = saturate(uParams.x);
    return float4(lerp(c.rgb, c.rgb * tint, strength), c.a);
}
]]></slang>
<glsl>
<vertex><![CDATA[
#version 330 core
out vec2 vUV;
void main() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    vUV = vec2(pos.x, 1.0 - pos.y);
}
]]></vertex>
<fragment><![CDATA[
#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform vec4 uParams;
out vec4 FragColor;
void main() {
    vec2 uv = vec2(vUV.x, 1.0 - vUV.y);
    vec4 c = texture(uScene, uv);
    float depth = texture(uDepth, uv).r;
    vec3 tint = mix(vec3(0.15, 0.25, 0.55), vec3(1.0, 0.9, 0.7), depth);
    float strength = clamp(uParams.x, 0.0, 1.0);
    FragColor = vec4(mix(c.rgb, c.rgb * tint, strength), c.a);
}
]]></fragment>
</glsl>
</shader>
