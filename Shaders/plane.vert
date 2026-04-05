#version 330 core
out vec3 worldPos;

layout(location = 0) in vec3 aPos;

uniform mat4 model, view, projection;

void main() {
    worldPos = aPos;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}