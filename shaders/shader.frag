#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform UBO {
    mat4 mvp;
    mat4 model;
    vec4 userColor;
    
    // Параметры для освещения
    vec3 lightDir;   // Направление света
    vec3 lightColor; // Цвет света
    float shininess; // Сила блика
} ubo;

void main(){
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-ubo.lightDir); // Важно: минус, если lightDir - направление луча
    
    float diff = max(dot(N, L), 0.0);
    outColor = vec4(vec3(diff), 1.0); // Просто покажем интенсивность света (от черного к белому)
}