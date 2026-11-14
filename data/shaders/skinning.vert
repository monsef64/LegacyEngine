#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec2 aInfluences;
uniform samplerBuffer uWeights;
uniform samplerBuffer uPalette;
uniform float uScale;
out vec3 skinnedPosition;
out vec3 skinnedNormal;
out vec2 skinnedUV;
mat4 bone(int i) {
    int j = i * 8;
    return mat4(texelFetch(uPalette, j),
                texelFetch(uPalette, j + 1),
                texelFetch(uPalette, j + 2),
                texelFetch(uPalette, j + 3));
}
void main() {
    vec3 p = vec3(0), n = vec3(0);
    float total = 0;
    for (uint i = 0u; i < aInfluences.y; ++i) {
        vec2 influence = texelFetch(uWeights, int(aInfluences.x + i)).xy;
        mat4 m = bone(int(influence.x));
        float w = influence.y;
        p += (m * vec4(aPosition, 1)).xyz * w;
        int j = int(influence.x) * 8 + 4;
        mat3 normalMatrix = mat3(texelFetch(uPalette, j).xyz,
                                 texelFetch(uPalette, j + 1).xyz,
                                 texelFetch(uPalette, j + 2).xyz);
        n += normalMatrix * aNormal * w;
        total += w;
    }
    skinnedPosition = p / max(total, 1e-20) * uScale;
    skinnedNormal = length(n) > 1e-12 ? normalize(n) : vec3(0, 1, 0);
    skinnedUV = aUV;
    gl_Position = vec4(skinnedPosition, 1);
}
