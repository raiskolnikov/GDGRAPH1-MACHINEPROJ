#version 330 core

in vec3 worldPos;
out vec4 FragColor;

uniform vec3 camPos;
uniform bool nightVision;

void main() {
    vec3 orangeColor = vec3(0.95, 0.45, 0.05);
    vec3 nvColor = vec3(0.05, 0.35, 0.05);

    vec3 baseColor = nightVision ? nvColor : orangeColor;

    // distance-based fog fading to horizon
    float dist = length(worldPos.xz - camPos.xz);
    float fog = 1.0 - exp(-dist * 0.012);
    fog = clamp(fog, 0.0, 1.0);

    // horizon color matches skybox mood
    vec3 horizonColor = nightVision ? vec3(0.02, 0.08, 0.02) : vec3(0.55, 0.25, 0.02);
    vec3 finalColor = mix(baseColor, horizonColor, fog);

    FragColor = vec4(finalColor, 1.0);
}