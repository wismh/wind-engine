<shader lang="slang">
<slang><![CDATA[
// The fluid surface composite (docs/tech/modules/Render.md, Fluids). A fullscreen pass over the scene: uFluid holds
// the splatted particles (r: density, g: density times foam), uBackdrop a copy of the scene before the water.
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
uniform Sampler2D uFluid;
[[vk::binding(2, 0)]]
uniform Sampler2D uBackdrop;
// rgb the water, a how much of it covers what is behind.
uniform float4 uColor;
// rgb the foam color.
uniform float4 uFoam;
// x density threshold of the surface, y edge softness, z refraction (how far, in pixels, the backdrop shifts at the
// surface's edge), w foam scale.
uniform float4 uParams;
uniform float2 uResolution;

[shader("fragment")]
float4 fragmentMain(VOut input) : SV_Target {
    float2 uv = input.vUV;
    float2 px = 1.0 / max(uResolution, float2(1.0, 1.0));
    float4 fluid = uFluid.Sample(uv);
    float density = fluid.r;
    float edge = max(uParams.y, 0.0001);
    float alpha = smoothstep(uParams.x - edge, uParams.x + edge, density);
    if (alpha <= 0.002) {
        discard;
    }
    // The edge band: 0 at the outside of the edge, 1 a little inside, where the bumps of single particles no longer
    // show. Refraction and the rim live in that band; the inside of the water is flat.
    float band_low = uParams.x - edge;
    float band_width = 3.0 * edge;
    float here = saturate((density - band_low) / band_width);
    float dx = saturate((uFluid.Sample(uv + float2(px.x, 0.0)).r - band_low) / band_width) -
               saturate((uFluid.Sample(uv - float2(px.x, 0.0)).r - band_low) / band_width);
    float dy = saturate((uFluid.Sample(uv + float2(0.0, px.y)).r - band_low) / band_width) -
               saturate((uFluid.Sample(uv - float2(0.0, px.y)).r - band_low) / band_width);
    float2 slope = float2(dx, dy);
    float slope_length = length(slope);
    float bump = 4.0 * here * (1.0 - here);
    float2 offset = slope_length > 0.0001 ? slope / slope_length * (uParams.z * bump) * px : float2(0.0, 0.0);
    float3 behind = uBackdrop.Sample(uv + offset).rgb;
    float foam = saturate(fluid.g / max(density, 0.0001) * uParams.w);
    float3 water = lerp(uColor.rgb, uFoam.rgb, foam);
    float cover = lerp(saturate(uColor.a), 1.0, foam);
    float3 color = lerp(behind, water, cover);
    color += bump * 0.25;
    return float4(color, alpha);
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
uniform sampler2D uFluid;
uniform sampler2D uBackdrop;
uniform vec4 uColor;
uniform vec4 uFoam;
uniform vec4 uParams;
uniform vec2 uResolution;
out vec4 FragColor;
void main() {
    vec2 uv = vec2(vUV.x, 1.0 - vUV.y);
    vec2 px = 1.0 / max(uResolution, vec2(1.0, 1.0));
    vec4 fluid = texture(uFluid, uv);
    float density = fluid.r;
    float edge = max(uParams.y, 0.0001);
    float alpha = smoothstep(uParams.x - edge, uParams.x + edge, density);
    if (alpha <= 0.002) {
        discard;
    }
    float band_low = uParams.x - edge;
    float band_width = 3.0 * edge;
    float here = clamp((density - band_low) / band_width, 0.0, 1.0);
    float dx = clamp((texture(uFluid, uv + vec2(px.x, 0.0)).r - band_low) / band_width, 0.0, 1.0) -
               clamp((texture(uFluid, uv - vec2(px.x, 0.0)).r - band_low) / band_width, 0.0, 1.0);
    float dy = clamp((texture(uFluid, uv + vec2(0.0, px.y)).r - band_low) / band_width, 0.0, 1.0) -
               clamp((texture(uFluid, uv - vec2(0.0, px.y)).r - band_low) / band_width, 0.0, 1.0);
    vec2 slope = vec2(dx, dy);
    float slope_length = length(slope);
    float bump = 4.0 * here * (1.0 - here);
    vec2 offset = slope_length > 0.0001 ? slope / slope_length * (uParams.z * bump) * px : vec2(0.0, 0.0);
    vec3 behind = texture(uBackdrop, uv + offset).rgb;
    float foam = clamp(fluid.g / max(density, 0.0001) * uParams.w, 0.0, 1.0);
    vec3 water = mix(uColor.rgb, uFoam.rgb, foam);
    float cover = mix(clamp(uColor.a, 0.0, 1.0), 1.0, foam);
    vec3 color = mix(behind, water, cover);
    color += bump * 0.25;
    FragColor = vec4(color, alpha);
}
]]></fragment>
</glsl>
</shader>
