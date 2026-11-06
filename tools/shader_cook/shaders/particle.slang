struct VertexInput {
    [[vk::location(0)]] float3 aPosition : POSITION;
    [[vk::location(1)]] float2 aUV : TEXCOORD0;
    [[vk::location(2)]] float3 aInstancePos : TEXCOORD1;
    [[vk::location(3)]] float aInstanceRot : TEXCOORD2;
    [[vk::location(4)]] float2 aInstanceSize : TEXCOORD3;
    [[vk::location(5)]] float4 aInstanceColor : COLOR0;
    [[vk::location(6)]] float2 aInstanceUvScale : TEXCOORD4;
    [[vk::location(7)]] float2 aInstanceUvOffset : TEXCOORD5;
    [[vk::location(8)]] float3 aInstanceVelocity : TEXCOORD6;
};
struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 vUV : TEXCOORD0;
    [[vk::location(1)]] float4 vColor : COLOR0;
};
uniform float4x4 uView;
uniform float4x4 uProjection;
uniform float4 uColor;
uniform int uParticleMode;
uniform Sampler2D uTexture;
[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput out;
    out.vUV = input.aUV * input.aInstanceUvScale + input.aInstanceUvOffset;
    out.vColor = input.aInstanceColor * uColor;
    float cos_r = cos(input.aInstanceRot);
    float sin_r = sin(input.aInstanceRot);
    float2 scaled = input.aPosition.xy * input.aInstanceSize;
    float2 rotated = float2(
        scaled.x * cos_r - scaled.y * sin_r,
        scaled.x * sin_r + scaled.y * cos_r);
    // Offset matches particle_card_offset. write_mat4 transposes row-major uView, so row 0 is camera right.
    float3 offset;
    if (uParticleMode == 0) {
        offset = float3(rotated, 0.0);
    } else {
        float3 camera_right = uView[0].xyz;
        float3 camera_up = uView[1].xyz;
        float right_len = length(camera_right);
        float up_len = length(camera_up);
        camera_right = right_len > 1e-8 ? camera_right / right_len : float3(1.0, 0.0, 0.0);
        camera_up = up_len > 1e-8 ? camera_up / up_len : float3(0.0, 1.0, 0.0);
        float3 axis_x = camera_right;
        float3 axis_y = camera_up;
        if (uParticleMode == 2) {
            float3 normal = cross(camera_right, camera_up);
            float nlen = length(normal);
            if (nlen > 1e-8) {
                normal /= nlen;
                float3 planar = camera_right * dot(input.aInstanceVelocity, camera_right)
                        + camera_up * dot(input.aInstanceVelocity, camera_up);
                float plen = length(planar);
                if (plen > 1e-4) {
                    axis_y = planar / plen;
                    axis_x = cross(axis_y, normal);
                }
            }
        }
        offset = axis_x * rotated.x + axis_y * rotated.y;
    }
    float3 world_pos = input.aInstancePos + offset;
    out.position = mul(uProjection, mul(uView, float4(world_pos, 1.0)));
    return out;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return uTexture.Sample(input.vUV) * input.vColor;
}
