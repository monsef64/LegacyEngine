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

void main()
{
    vNormal = uNormalMatrix * aNormal;
    vWorldPosition = (uModel * vec4(aPosition, 1.0)).xyz;
    // A 2x2 repeat covers 2 metres: each texel is one checker square.
    vCheckerUV = aPosition.xz * 0.5;
    vUV=aUV;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
