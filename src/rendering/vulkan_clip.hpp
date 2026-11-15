// Existing engine camera matrices use a -w..w depth range; Vulkan clips
// depth to 0..w instead.
#pragma once

#include "core/math.hpp"

// Convert an engine clip-space matrix for a Vulkan pipeline using a
// positive-height viewport. Y is inverted to preserve the engine's screen
// orientation. The pipeline's front-face convention must account for both this
// flip and Vulkan viewport orientation. Apply the conversion exactly once,
// after the projection/view (and optionally model) multiplication.
// Keep the original matrix for CPU camera queries.
inline Mat4 vulkanClipMatrix(const Mat4& engineClipMatrix) {
    Mat4 result = engineClipMatrix;
    for (int column = 0; column < 4; ++column) {
        const int i = column * 4;
        result.m[i + 1] = -engineClipMatrix.m[i + 1];
        result.m[i + 2] = (engineClipMatrix.m[i + 2] + engineClipMatrix.m[i + 3]) * 0.5f;
    }
    return result;
}
