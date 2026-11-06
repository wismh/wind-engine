// The forward light list. Same layout as engine::render::pack_forward_lights and the same count as
// engine::render::kMaxForwardLights.

uniform int uLightCount;
uniform float4 uLightColorKind[16];
uniform float4 uLightDirRange[16];
uniform float4 uLightPosSpot[16];
uniform float4 uLightSpotBias[16];
uniform float3 uAmbient;
uniform float3 uCameraPosition;
uniform float uGloss;
uniform int uHasNormalMap;
uniform Sampler2D uNormal;

float3 perturb_normal(float3 n, float3 worldPos, float2 uv, float3 tangentNormal) {
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 N = normalize(n);
    float3 T = dp1 * duv2.y - dp2 * duv1.y;
    float tlen = length(T);
    if (tlen < 0.000001) {
        return N;
    }
    T = T / tlen;
    float3 B = normalize(cross(N, T));
    float3 tn = tangentNormal * 2.0 - 1.0;
    return normalize(T * tn.x + B * tn.y + N * tn.z);
}

float3 forward_shade(float3 albedo, float3 normal, float3 worldPos, bool lambert) {
    float3 lit = uAmbient;
    float3 spec = float3(0.0, 0.0, 0.0);
    float3 viewDir = uCameraPosition - worldPos;
    float viewLen = length(viewDir);
    float3 V = viewLen > 0.000001 ? viewDir / viewLen : float3(0.0, 0.0, 1.0);
    float3 N = normalize(normal);
    for (int i = 0; i < 16; ++i) {
        if (i >= uLightCount) {
            break;
        }
        float kind = uLightColorKind[i].a;
        float3 color = uLightColorKind[i].rgb;
        float3 travel = uLightDirRange[i].xyz;
        float range = uLightDirRange[i].w;
        float3 pos = uLightPosSpot[i].xyz;
        float spotScale = uLightPosSpot[i].w;
        float spotBias = uLightSpotBias[i].x;
        float3 L;
        float atten = 1.0;
        float spot = 1.0;
        if (kind < 0.5) {
            L = -travel;
        } else {
            float3 toLight = pos - worldPos;
            float dist = length(toLight);
            L = dist > 0.000001 ? toLight / dist : float3(0.0, 0.0, 1.0);
            float ratio = dist / max(range, 0.000001);
            float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
            window = window * window;
            atten = window / (dist * dist + 1.0);
            if (kind > 1.5) {
                spot = clamp(dot(-L, travel) * spotScale + spotBias, 0.0, 1.0);
            }
        }
        float ndotl = lambert ? max(dot(N, L), 0.0) : 1.0;
        float contrib = atten * spot * ndotl;
        lit += color * contrib;
        if (lambert && uGloss > 0.0 && ndotl > 0.0) {
            float3 H = normalize(L + V);
            spec += color * contrib * pow(max(dot(N, H), 0.0), uGloss);
        }
    }
    return albedo * lit + spec;
}
