<shader lang="slang">
<slang><![CDATA[
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
]]></slang>
<glsl>
    <vertex><![CDATA[
        #version 330 core

        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec2 aUV;
        layout(location = 2) in vec3 aAnim;

        out vec2 vUV;

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
            gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
        }
    ]]></vertex>

    <fragment><![CDATA[
        #version 330 core

        out vec4 FragColor;

        in vec2 vUV;
        uniform sampler2D uTexture;
        uniform vec4 uColor;

        void main() {
            FragColor = texture(uTexture, vUV) * uColor;
        }
    ]]></fragment>
</glsl>
</shader>
