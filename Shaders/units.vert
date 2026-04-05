#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec2 aUV;

out vec3 fragPos;
out vec3 fragNorm;
out vec2 fragUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    fragPos = worldPos.xyz;
    fragNorm = mat3(transpose(inverse(model))) * aNorm;
    fragUV = aUV;
    gl_Position = projection * view * worldPos;
}