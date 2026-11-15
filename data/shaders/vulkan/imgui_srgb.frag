#version 450
layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D sTexture;
layout(location = 0) in struct {
    vec4 Color;
    vec2 UV;
} In;

// ImGui colors and the engine's UNORM images are display-encoded. Undo that
// encoding before an sRGB attachment applies it again, preserving brightness.
void main() {
    vec4 color = In.Color * texture(sTexture, In.UV.st);
    vec3 linearLow = color.rgb / 12.92;
    vec3 linearHigh = pow((color.rgb + 0.055) / 1.055, vec3(2.4));
    fColor = vec4(mix(linearHigh, linearLow, lessThanEqual(color.rgb, vec3(0.04045))), color.a);
}
