#version 450
layout(location = 0) in vec3 aPosition;
layout(push_constant) uniform ShadowDraw {
    mat4 modelViewProjection;
}
draw;
void main() {
    gl_Position = draw.modelViewProjection * vec4(aPosition, 1.0);
}
