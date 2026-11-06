// The forward light list. Same layout as engine::render::pack_forward_lights and the same count as
// engine::render::kMaxForwardLights. Paste this into a fragment shader; do not declare these uniforms again.

uniform int uLightCount;
uniform vec4 uLightColorKind[16];
uniform vec4 uLightDirRange[16];
uniform vec4 uLightPosSpot[16];
uniform vec4 uLightSpotBias[16];
uniform vec3 uAmbient;
uniform vec3 uCameraPosition;
uniform float uGloss;
uniform int uHasNormalMap;
uniform sampler2D uNormal;

vec3 perturb_normal(vec3 n, vec3 worldPos, vec2 uv, vec3 tangentNormal) {
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 N = normalize(n);
    vec3 T = dp1 * duv2.y - dp2 * duv1.y;
    float tlen = length(T);
    if (tlen < 0.000001) {
        return N;
    }
    T = T / tlen;
    vec3 B = normalize(cross(N, T));
    vec3 tn = tangentNormal * 2.0 - 1.0;
    return normalize(T * tn.x + B * tn.y + N * tn.z);
}

// `lambert` false is a sprite with no normal map: distance and cone only, because a quad's +Z normal is
// perpendicular to a light in the sprite plane.
vec3 forward_shade(vec3 albedo, vec3 normal, vec3 worldPos, bool lambert) {
    vec3 lit = uAmbient;
    vec3 spec = vec3(0.0);
    vec3 viewDir = uCameraPosition - worldPos;
    float viewLen = length(viewDir);
    vec3 V = viewLen > 0.000001 ? viewDir / viewLen : vec3(0.0, 0.0, 1.0);
    vec3 N = normalize(normal);
    for (int i = 0; i < 16; ++i) {
        if (i >= uLightCount) {
            break;
        }
        float kind = uLightColorKind[i].a;
        vec3 color = uLightColorKind[i].rgb;
        vec3 travel = uLightDirRange[i].xyz;
        float range = uLightDirRange[i].w;
        vec3 pos = uLightPosSpot[i].xyz;
        float spotScale = uLightPosSpot[i].w;
        float spotBias = uLightSpotBias[i].x;
        vec3 L;
        float atten = 1.0;
        float spot = 1.0;
        if (kind < 0.5) {
            L = -travel;
        } else {
            vec3 toLight = pos - worldPos;
            float dist = length(toLight);
            L = dist > 0.000001 ? toLight / dist : vec3(0.0, 0.0, 1.0);
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
            vec3 H = normalize(L + V);
            spec += color * contrib * pow(max(dot(N, H), 0.0), uGloss);
        }
    }
    return albedo * lit + spec;
}
