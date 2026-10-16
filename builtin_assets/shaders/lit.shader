<xml>
    <vertex><![CDATA[
        #version 330 core

        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec2 aUV;
        layout(location = 2) in vec3 aNormal;

        out vec2 vUV;
        out vec3 vNormal;

        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        // Inverse transpose of mat3(uModel), set by the backend for every mesh draw.
        uniform mat3 uNormalMatrix;
        uniform vec2 uUvScale;
        uniform vec2 uUvOffset;

        void main() {
            vUV = aUV * uUvScale + uUvOffset;
            vNormal = uNormalMatrix * aNormal;
            gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
        }
    ]]></vertex>

    <fragment><![CDATA[
        #version 330 core

        // Same number as engine::render::kMaxDirectionalLights (include/engine/render/scene_lights.h).
        #define kMaxDirectionalLights 4

        out vec4 FragColor;

        in vec2 vUV;
        in vec3 vNormal;

        uniform sampler2D uTexture;
        uniform vec4 uColor;
        uniform int uLightCount;
        // World-space directions the lights travel along (unit length), and color * intensity.
        uniform vec3 uLightDirections[kMaxDirectionalLights];
        uniform vec3 uLightColors[kMaxDirectionalLights];
        uniform vec3 uAmbient;

        void main() {
            vec4 albedo = texture(uTexture, vUV) * uColor;
            vec3 normal = normalize(vNormal);
            vec3 light = uAmbient;
            for (int i = 0; i < kMaxDirectionalLights; ++i) {
                if (i >= uLightCount) {
                    break;
                }
                light += uLightColors[i] * max(dot(normal, -uLightDirections[i]), 0.0);
            }
            FragColor = vec4(albedo.rgb * light, albedo.a);
        }
    ]]></fragment>
</xml>
