#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aSize;
layout(location = 2) in float aRotation;
layout(location = 3) in vec3 aColor;
layout(location = 4) in float aAlpha;
layout(location = 5) in float aFrame;
uniform mat4 uVP;
uniform vec3 uRight, uUp;
uniform ivec2 uSheet;
out vec2 vUV;
out vec4 vColor;
void main() {
    const vec2 corners[6] =
        vec2[6](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));
    vec2 uv = corners[gl_VertexID], p = (uv - .5) * aSize;
    float c = cos(aRotation), s = sin(aRotation);
    vec2 rotated = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    gl_Position = uVP * vec4(aPosition + uRight * rotated.x + uUp * rotated.y, 1);
    int frame = int(mod(aFrame, float(uSheet.x * uSheet.y)));
    vUV = (vec2(frame % uSheet.x, uSheet.y - 1 - frame / uSheet.x) + uv) / vec2(uSheet);
    vColor = vec4(aColor, aAlpha);
}
