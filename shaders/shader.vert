#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;

layout(binding = 0) uniform UBO {
    mat4 mvp;
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 albedo;
    vec4 lightDir;
    vec4 lightColor;
    vec4 cameraPos;
    vec4 specColor;
    vec4 ambient;
} ubo;

void main() {
    gl_Position = ubo.mvp * vec4(inPosition, 1.0);
    fragPos = vec3(ubo.model * vec4(inPosition, 1.0));
    fragNormal = mat3(ubo.model) * inNormal;
    fragColor = inColor;
}