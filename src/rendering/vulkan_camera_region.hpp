#pragma once
#include "rendering/vulkan_scene_surface.hpp"
#include "scene/scene_camera.hpp"
#include <algorithm>
#include <stdexcept>

// Camera viewport coordinates are normalized from the top-left. Vulkan's
// positive-height viewport uses the same origin (the shader projection flips Y).
inline VulkanSceneSurface::RenderRegion
vulkanCameraRegion(const Camera& camera, uint32_t width, uint32_t height) {
    if (!width || !height || width > 8192 || height > 8192 || !validCamera(camera)) {
        throw std::runtime_error("Invalid Vulkan camera viewport");
    }
    const uint32_t x = std::min(width - 1, uint32_t(camera.viewportX * width));
    const uint32_t y = std::min(height - 1, uint32_t(camera.viewportY * height));
    return {x,
            y,
            std::min(width - x, std::max(1u, uint32_t(camera.viewportWidth * width))),
            std::min(height - y, std::max(1u, uint32_t(camera.viewportHeight * height)))};
}
