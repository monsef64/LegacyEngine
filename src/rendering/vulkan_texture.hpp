// Vulkan-owned RGBA image for editor thumbnails and material textures.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanTexture {
  public:
    enum class Address { Clamp, Repeat, Panorama };
    // Pixels are tightly packed RGBA8, top to bottom; no implicit vertical flip.
    // Mipmaps use linear blits and require the format's blit/filter features.
    // The device and graphics queue must outlive this texture. This constructor
    // waits for its upload; calls/queue use must be externally synchronized.
    VulkanTexture(VkPhysicalDevice physical,
                  VkDevice device,
                  VkQueue queue,
                  uint32_t queueFamily,
                  uint32_t width,
                  uint32_t height,
                  const std::vector<uint8_t>& rgba,
                  Address address = Address::Clamp,
                  bool mipmaps = false,
                  bool nearestMagnification = false,
                  bool cubemap = false);
    // HDR RGBA32F panorama. Floating point values are uploaded without
    // quantization; device support for linear filtering is required.
    VulkanTexture(VkPhysicalDevice physical,
                  VkDevice device,
                  VkQueue queue,
                  uint32_t queueFamily,
                  uint32_t width,
                  uint32_t height,
                  const std::vector<float>& rgba,
                  Address address = Address::Clamp);
    ~VulkanTexture();
    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    // The image is owned by this object; callers must synchronize any direct
    // access and return all mip levels to imageLayout() before sampling. It has
    // TRANSFER_SRC usage only when multiple mip levels were generated.
    VkImage image() const {
        return image_;
    }
    VkImageView imageView() const {
        return view_;
    }
    VkSampler sampler() const {
        return sampler_;
    }
    VkImageLayout imageLayout() const {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    uint32_t width() const {
        return width_;
    }
    uint32_t height() const {
        return height_;
    }
    uint32_t mipLevels() const {
        return mipLevels_;
    }
    bool cubemap() const {
        return cubemap_;
    }

  private:
    void initialize(VkPhysicalDevice physical,
                    VkQueue queue,
                    uint32_t queueFamily,
                    const void* pixels,
                    std::size_t bytes,
                    VkFormat format,
                    Address address,
                    bool mipmaps,
                    bool nearestMagnification);
    void cleanup() noexcept;
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    uint32_t width_ = 0, height_ = 0, mipLevels_ = 0;
    bool cubemap_ = false;
};
