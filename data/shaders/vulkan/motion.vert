#version 450
layout(location = 0) in vec3 currentPosition;
layout(location = 1) in vec3 previousPosition;
layout(set = 0, binding = 0, std140) uniform MotionDraw {
    mat4 rasterMVP;
    mat4 currentMVP;
    mat4 previousMVP;
} draw;
layout(location = 0) out vec4 currentClip;
layout(location = 1) out vec4 previousClip;
void main() {
    gl_Position = draw.rasterMVP * vec4(currentPosition, 1.0);
    currentClip = draw.currentMVP * vec4(currentPosition, 1.0);
    previousClip = draw.previousMVP * vec4(previousPosition, 1.0);
}
