#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModelViewProjection;
uniform mat3 uNormalMatrix;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vWorldPosition;
out vec2 vCheckerUV;
out vec2 vUV;
out vec2 vLightmapUV;
uniform int uLightmapGrid;
uniform sampler2D uLightmap;

void main() {
    vNormal = uNormalMatrix * aNormal;
    vWorldPosition = (uModel * vec4(aPosition, 1.0)).xyz;
    // A 2x2 repeat covers 2 metres: each texel is one checker square.
    vCheckerUV = aPosition.xz * 0.5;
    vUV = aUV;
    vLightmapUV = vec2(0);
    if (uLightmapGrid > 0) {
        int triangle = gl_VertexID / 3, corner = gl_VertexID % 3;
        float tile = float(textureSize(uLightmap, 0).x / uLightmapGrid);
        float gutter = tile >= 6.0 ? 2.5 : 0.5;
        vec2 local = corner == 0 ? vec2(gutter) : (corner == 1 ? vec2(tile - gutter, gutter) : vec2(gutter, tile - gutter));
        vLightmapUV = (vec2(triangle % uLightmapGrid, triangle / uLightmapGrid) * tile + local) / vec2(textureSize(uLightmap, 0));
    }
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
