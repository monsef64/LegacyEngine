#version 450
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(set = 0, binding = 0, std140) uniform Object {
    mat4 modelViewProjection;
    mat4 model;
    mat4 normalMatrix;
}
object;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorldPosition;
layout(location = 2) out vec2 vCheckerUV;
layout(location = 3) out vec2 vUV;

void main() {
    vNormal = mat3(object.normalMatrix) * aNormal;
    vWorldPosition = (object.model * vec4(aPosition, 1.0)).xyz;
    vCheckerUV = aPosition.xz * 0.5;
    vUV = aUV;
    gl_Position = object.modelViewProjection * vec4(aPosition, 1.0);
}
