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
uniform float2 uResolution;

[shader("fragment")]
float4 fragmentMain(VOut input) : SV_Target {
    float2 uv = input.vUV;
    float3 c = uScene.Sample(uv).rgb;
    int mode = int(uParams.z + 0.5);
    if (mode == 0) {
        return float4(max(c - float3(uParams.x, uParams.x, uParams.x), float3(0.0, 0.0, 0.0)), 1.0);
    }
    if (mode == 1) {
        float2 texel = float2(1.0, 1.0) / max(uResolution, float2(1.0, 1.0));
        float3 sum = uScene.Sample(uv + float2(-texel.x, -texel.y)).rgb
                   + uScene.Sample(uv + float2(texel.x, -texel.y)).rgb
                   + uScene.Sample(uv + float2(-texel.x, texel.y)).rgb
                   + uScene.Sample(uv + float2(texel.x, texel.y)).rgb;
        return float4(sum * 0.25, 1.0);
    }
    if (mode == 2) {
        return float4(c * uParams.y, 1.0);
    }
    return float4(c, uScene.Sample(uv).a);
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
uniform vec2 uResolution;
out vec4 FragColor;
void main() {
    vec2 uv = vec2(vUV.x, 1.0 - vUV.y);
    vec3 c = texture(uScene, uv).rgb;
    int mode = int(uParams.z + 0.5);
    if (mode == 0) {
        FragColor = vec4(max(c - vec3(uParams.x), vec3(0.0)), 1.0);
        return;
    }
    if (mode == 1) {
        vec2 texel = vec2(1.0) / max(uResolution, vec2(1.0));
        vec3 sum = texture(uScene, uv + vec2(-texel.x, -texel.y)).rgb
                 + texture(uScene, uv + vec2(texel.x, -texel.y)).rgb
                 + texture(uScene, uv + vec2(-texel.x, texel.y)).rgb
                 + texture(uScene, uv + vec2(texel.x, texel.y)).rgb;
        FragColor = vec4(sum * 0.25, 1.0);
        return;
    }
    if (mode == 2) {
        FragColor = vec4(c * uParams.y, 1.0);
        return;
    }
    FragColor = vec4(c, texture(uScene, uv).a);
}
]]></fragment>
</glsl>
</shader>
