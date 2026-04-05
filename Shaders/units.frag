#version 330 core

in vec3 fragPos;
in vec3 fragNorm;
in vec2 fragUV;

out vec4 FragColor;

uniform sampler2D tex0;

// direction light
struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
};
uniform DirLight dirLight;

// point light
struct PtLight {
    vec3 position;
    vec3 color;
    float intensity;
    float constant;
    float linear;
    float quadratic;
};

uniform PtLight pointLight;

uniform vec3 ambientColor;

void main() {
    vec4 texColor = texture(tex0, fragUV);
    vec3 norm = normalize(fragNorm);

    // directional
    float diffDir = max(dot(norm, -dirLight.direction), 0.0);
    vec3 dirContrib = dirLight.color * dirLight.intensity * diffDir;

    // point
    vec3  toLight = pointLight.position - fragPos;
    float dist = length(toLight);
    float atten = 1.0 / (pointLight.constant
                          + pointLight.linear    * dist
                          + pointLight.quadratic * dist * dist);
    float diffPt = max(dot(norm, normalize(toLight)), 0.0);
    vec3 ptContrib = pointLight.color * pointLight.intensity * diffPt * atten;

    vec3 lighting = ambientColor + dirContrib + ptContrib;
    FragColor = vec4(lighting * texColor.rgb, texColor.a);
}