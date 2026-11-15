#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0, std140) uniform Skybox {
    mat4 inverseVP;
    vec4 tintOpacity;
    int hasTexture;
    int isHDR;
}
sky;
layout(set = 0, binding = 1) uniform samplerCube faces;
layout(set = 0, binding = 2) uniform sampler2D panorama;
void main() {
    // inverseVP must be the inverse of the converted Vulkan view-projection.
    vec4 nearP = sky.inverseVP * vec4(uv, 0, 1), farP = sky.inverseVP * vec4(uv, 1, 1);
    vec3 d = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);
    vec3 c = vec3(1);
    if (sky.hasTexture != 0) {
        if (sky.isHDR != 0) {
            vec2 p = vec2(atan(d.z, d.x) / 6.28318530718 + 0.5,
                          0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.14159265359);
            c = texture(panorama, p).rgb;
        } else {
            c = texture(faces, d).rgb;
        }
    }
    c = max(c * sky.tintOpacity.rgb, vec3(0));
    if (sky.isHDR != 0 && sky.hasTexture != 0) {
        c = pow(c / (c + vec3(1)), vec3(1.0 / 2.2));
    }
    fragColor = vec4(c, sky.tintOpacity.a);
}
