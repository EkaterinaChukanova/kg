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
    vec4 userColor;
    vec4 lightDir;
    vec4 lightColor;
    vec4 cameraPos;
} ubo;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-ubo.lightDir.xyz);
    vec3 V = normalize(ubo.cameraPos.xyz - fragPos);
    vec3 H = normalize(L + V);

    float diff = pow(max(dot(N, L), 0.0), 1.4);
    float shininess = max(ubo.lightColor.w, 1.0);
    float spec = pow(max(dot(N, H), 0.0), shininess);

    vec3 albedo = fragColor * ubo.userColor.rgb;
    vec3 color = albedo * 0.06
            + diff * albedo * ubo.lightColor.rgb
            + spec * ubo.lightColor.rgb;
    outColor = vec4(color, 1.0);
}