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
uniform float4 uParams;

[shader("fragment")]
float4 fragmentMain(VOut input) : SV_Target {
    float4 c = uScene.Sample(input.vUV);
    return float4(c.rgb * uParams.rgb, c.a);
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
uniform vec4 uParams;
out vec4 FragColor;
void main() {
    vec4 c = texture(uScene, vec2(vUV.x, 1.0 - vUV.y));
    FragColor = vec4(c.rgb * uParams.rgb, c.a);
}
]]></fragment>
</glsl>
</shader>
