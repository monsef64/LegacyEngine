#version 330 core
uniform mat4 inverseVP;
out vec3 viewRay;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vec2 uv = p * 2.0 - 1.0;
    vec4 nearP = inverseVP * vec4(uv, -1, 1), farP = inverseVP * vec4(uv, 1, 1);
    viewRay = farP.xyz / farP.w - nearP.xyz / nearP.w;
    gl_Position = vec4(uv, 1, 1);
}
