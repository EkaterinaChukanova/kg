#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform UBO {
    mat4 mvp;
    vec4 userColor;
} ubo;

void main(){
    outColor = vec4(fragColor * ubo.userColor.rgb, 1.0);
}