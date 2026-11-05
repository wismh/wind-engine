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

[shader("fragment")]
float4 fragmentMain(VOut input) : SV_Target {
    float4 c = uScene.Sample(input.vUV);
    float3 mapped = c.rgb / (c.rgb + float3(1.0, 1.0, 1.0));
    return float4(mapped, c.a);
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
out vec4 FragColor;
void main() {
    vec4 c = texture(uScene, vec2(vUV.x, 1.0 - vUV.y));
    vec3 mapped = c.rgb / (c.rgb + vec3(1.0));
    FragColor = vec4(mapped, c.a);
}
]]></fragment>
</glsl>
</shader>
