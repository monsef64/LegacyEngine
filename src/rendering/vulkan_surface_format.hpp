#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <vulkan/vulkan.h>

// The editor and its offscreen textures use display-encoded colors. A UNORM
// swapchain preserves them; an sRGB attachment otherwise encodes them twice.
inline bool vulkanSrgbFormat(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB ||
           format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}
inline VkSurfaceFormatKHR
chooseEditorSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        return {VK_FORMAT_B8G8R8A8_UNORM, formats[0].colorSpace};
    }
    for (auto preferred : {VK_FORMAT_B8G8R8A8_UNORM,
                           VK_FORMAT_R8G8B8A8_UNORM,
                           VK_FORMAT_B8G8R8A8_SRGB,
                           VK_FORMAT_R8G8B8A8_SRGB}) {
        for (const auto& format : formats) {
            if (format.format == preferred &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
    }
    return formats.front(); // Caller has checked that the list is not empty.
}
inline float editorSrgbToLinear(float color) {
    return color <= 0.04045f ? color / 12.92f : std::pow((color + 0.055f) / 1.055f, 2.4f);
}
