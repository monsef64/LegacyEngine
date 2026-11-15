#version 450
layout(push_constant) uniform Draw {
    mat4 modelViewProjection;
    vec4 color;
}
draw;
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = draw.color;
}
