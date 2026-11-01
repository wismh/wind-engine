<shader lang="slang"><![CDATA[
struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(2)]] float3 aAnim : NORMAL;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
};

uniform float4x4 uModel;
uniform float4x4 uView;
uniform float4x4 uProjection;
uniform float2 uUvScale;
uniform float2 uUvOffset;
uniform float uTime;
uniform Sampler2D uTexture;
uniform float4 uColor;

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
    out.position = mul(uProjection, mul(uView, mul(uModel, float4(input.aPosition, 1.0))));
    return out;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return uTexture.Sample(input.vUV) * uColor;
}
]]></shader>
