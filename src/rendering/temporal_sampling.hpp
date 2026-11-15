// Sampling helpers for a future temporal upscaler. This is not an upscaler;
// its output must not be exposed as an FSR setting until the SDK and the
// required color, depth and motion-vector resources are connected.
#pragma once

#include "core/math.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace TemporalSampling {
// AMD FSR 3's recommended per-dimension ratios (including native AA).
enum class Quality { NativeAA, Quality, Balanced, Performance, UltraPerformance };
struct Extent {
    uint32_t width = 0, height = 0;
};
struct Offset {
    float x = 0, y = 0; // render-resolution pixels
};
inline bool operator==(Extent a, Extent b) {
    return a.width == b.width && a.height == b.height;
}
inline float ratio(Quality quality) {
    switch (quality) {
    case Quality::NativeAA:
        return 1.0f;
    case Quality::Quality:
        return 1.5f;
    case Quality::Balanced:
        return 1.7f;
    case Quality::Performance:
        return 2.0f;
    case Quality::UltraPerformance:
        return 3.0f;
    }
    throw std::invalid_argument("Invalid temporal upscaling quality");
}
inline Extent renderExtent(Extent display, Quality quality) {
    // FSR's width and phase APIs use signed 32-bit integers. The actual
    // device image-size limit must also be checked when allocating images.
    if (!display.width || !display.height || display.width > INT32_MAX ||
        display.height > INT32_MAX) {
        throw std::invalid_argument("Invalid temporal upscaling display extent");
    }
    const float scale = ratio(quality);
    // A tiny editor view must never create a zero-sized Vulkan image.
    return {std::max(1u, uint32_t(float(display.width) / scale)),
            std::max(1u, uint32_t(float(display.height) / scale))};
}
inline uint32_t phaseCount(uint32_t renderWidth, uint32_t displayWidth) {
    if (!renderWidth || renderWidth > displayWidth || displayWidth > INT32_MAX) {
        throw std::invalid_argument("Invalid temporal upscaling widths");
    }
    const float scale = float(displayWidth) / float(renderWidth);
    const float phases = 8.0f * scale * scale;
    if (phases >= 2147483648.0f) {
        throw std::invalid_argument("Temporal jitter phase count is too large");
    }
    return uint32_t(phases);
}
inline float halton(uint32_t index, uint32_t base) {
    if (base < 2) {
        throw std::invalid_argument("Halton base must be at least two");
    }
    float fraction = 1, result = 0;
    while (index) {
        fraction /= float(base);
        result += fraction * float(index % base);
        index /= base;
    }
    return result;
}
inline Offset jitter(uint64_t frame, uint32_t phases) {
    if (!phases || phases > INT32_MAX) {
        throw std::invalid_argument("Temporal jitter needs at least one phase");
    }
    const auto index = uint32_t(frame % phases) + 1;
    return {halton(index, 2) - .5f, halton(index, 3) - .5f};
}
// Translate engine clip X/Y by the sample's subpixel offset. The engine's
// Vulkan conversion subsequently flips Y. The opposite sign here makes a
// positive jitter move geometry down in the top-left-origin Vulkan image, as
// expected by FSR pixel-space jitter. This works for perspective,
// orthographic and already-composed view-projection matrices alike.
inline Mat4 jitterViewProjection(const Mat4& viewProjection, Offset offset, Extent render) {
    if (!render.width || !render.height) {
        throw std::invalid_argument("Temporal jitter needs a nonzero render extent");
    }
    const float dx = 2 * offset.x / float(render.width);
    const float dy = -2 * offset.y / float(render.height);
    Mat4 out = viewProjection;
    for (int column = 0; column < 4; ++column) {
        const int i = column * 4;
        out.m[i] += dx * viewProjection.m[i + 3];
        out.m[i + 1] += dy * viewProjection.m[i + 3];
    }
    return out;
}
} // namespace TemporalSampling
