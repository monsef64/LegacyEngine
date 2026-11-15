#version 450
layout(location = 0) in vec4 currentClip;
layout(location = 1) in vec4 previousClip;
layout(location = 0) out vec2 motion;
void main() {
    // Vulkan NDC and the image both have positive Y down after the engine's
    // clip conversion. Store unjittered previous UV minus current UV.
    if (currentClip.w <= 0.0 || previousClip.w <= 0.0) {
        motion = vec2(0.0);
    } else {
        motion = 0.5 * (previousClip.xy / previousClip.w - currentClip.xy / currentClip.w);
    }
}
