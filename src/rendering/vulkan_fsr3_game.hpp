// Experimental Game-view-only FSR 3 Vulkan adapter. The SDK implementation is
// linked into a separate shared library to keep its Vulkan wrappers private.
#pragma once
#include "rendering/temporal_sampling.hpp"
#include <memory>
#include <vulkan/vulkan.h>

class VulkanFsr3Game {
  public:
    struct Device {
        VkInstance instance;
        VkPhysicalDevice physical;
        VkDevice device;
        VkQueue queue;
        uint32_t queueFamily;
        uint32_t apiVersion;
    };
    struct Inputs {
        VkImage color, depth, motion, destination;
        TemporalSampling::Offset jitter;
        bool reset;
        float frameTimeMs, cameraNear, cameraFar, cameraFovRadians;
    };
    // One instance per process/device: the community backend's host feature
    // shim is global. Calls and all access to the queue must be synchronized.
    VulkanFsr3Game(Device device,
                   TemporalSampling::Extent render,
                   TemporalSampling::Extent display);
    ~VulkanFsr3Game();
    VulkanFsr3Game(const VulkanFsr3Game&) = delete;
    VulkanFsr3Game& operator=(const VulkanFsr3Game&) = delete;
    // Inputs are engine surface images in their documented sampled layouts.
    // Destination is a display-sized RGBA8 transfer destination, in
    // SHADER_READ_ONLY_OPTIMAL. Waits for completion and restores layouts.
    // If this throws, destroy the adapter and invalidate temporal history.
    void dispatch(const Inputs& inputs);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
