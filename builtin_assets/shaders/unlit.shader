<xml>
    <vertex>
        #version 330 core

        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec2 aUV;

        out vec2 vUV;

        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;

        void main() {
            vUV = aUV;
            gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
        }
    </vertex>

    <fragment>
        #version 330 core

        out vec4 FragColor;

        in vec2 vUV;
        uniform sampler2D uTexture;
        uniform vec4 uColor;

        void main() {
            FragColor = texture(uTexture, vUV) * uColor;
        }
    </fragment>
</xml>
