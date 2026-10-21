#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform UBO {
    mat4 mvp;
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 albedo;
    vec4 lightDir;
    vec4 lightColor; // xyz — цвет, w — интенсивность
    vec4 cameraPos;
    vec4 specColor;  // xyz — цвет блика, w — коэффициент блика
    vec4 ambient;
} ubo;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-ubo.lightDir.xyz);
    vec3 V = normalize(ubo.cameraPos.xyz - fragPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float shininess = max(ubo.specColor.w, 1.0);
    float spec = pow(max(dot(N, H), 0.0), shininess);
    spec *= step(0.0, dot(N, L));
    vec3 light = ubo.lightColor.rgb * ubo.lightColor.w;

    vec3 base = fragColor * ubo.albedo.rgb;
    vec3 color = ubo.ambient.rgb * base
               + diff * base * light
               + spec * ubo.specColor.rgb * light;
    outColor = vec4(color, 1.0);
}