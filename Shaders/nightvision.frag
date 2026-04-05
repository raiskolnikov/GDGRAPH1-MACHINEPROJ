#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D overlayTex;

void main() {
    vec4 tex = texture(overlayTex, TexCoord);

    // adjust intensity
    FragColor = vec4(tex.rgb, 0.3); // transparency
}