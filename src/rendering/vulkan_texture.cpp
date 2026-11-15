#include "rendering/vulkan_texture.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}
uint32_t memoryType(VkPhysicalDevice physical,
                    uint32_t bits,
                    VkMemoryPropertyFlags required,
                    VkMemoryPropertyFlags preferred = 0) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        auto flags = props.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) && (flags & required) == required) {
            if ((flags & preferred) == preferred) {
                return i;
            }
            if (fallback == UINT32_MAX) {
                fallback = i;
            }
        }
    }
    if (fallback == UINT32_MAX) {
        throw std::runtime_error("No compatible Vulkan memory type");
    }
    return fallback;
}
struct UploadResources {
    VkDevice device;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    ~UploadResources() {
        if (pool) {
            vkDestroyCommandPool(device, pool, nullptr);
        }
        if (buffer) {
            vkDestroyBuffer(device, buffer, nullptr);
        }
        if (memory) {
            vkFreeMemory(device, memory, nullptr);
        }
    }
};
} // namespace
VulkanTexture::VulkanTexture(VkPhysicalDevice physical,
                             VkDevice device,
                             VkQueue queue,
                             uint32_t queueFamily,
                             uint32_t width,
                             uint32_t height,
                             const std::vector<uint8_t>& rgba,
                             Address address,
                             bool mipmaps,
                             bool nearestMagnification,
                             bool cubemap)
    : device_(device), width_(width), height_(height), cubemap_(cubemap) {
    initialize(physical,
               queue,
               queueFamily,
               rgba.data(),
               rgba.size(),
               VK_FORMAT_R8G8B8A8_UNORM,
               address,
               mipmaps,
               nearestMagnification);
}
VulkanTexture::VulkanTexture(VkPhysicalDevice physical,
                             VkDevice device,
                             VkQueue queue,
                             uint32_t queueFamily,
                             uint32_t width,
                             uint32_t height,
                             const std::vector<float>& rgba,
                             Address address)
    : device_(device), width_(width), height_(height) {
    initialize(physical,
               queue,
               queueFamily,
               rgba.data(),
               rgba.size() * sizeof(float),
               VK_FORMAT_R32G32B32A32_SFLOAT,
               address,
               false,
               false);
}
void VulkanTexture::initialize(VkPhysicalDevice physical,
                               VkQueue queue,
                               uint32_t queueFamily,
                               const void* pixels,
                               std::size_t bytes,
                               VkFormat pixelFormat,
                               Address address,
                               bool mipmaps,
                               bool nearestMagnification) {
    const uint32_t width = width_, height = height_, layers = cubemap_ ? 6 : 1;
    const uint64_t bytesPerPixel = pixelFormat == VK_FORMAT_R32G32B32A32_SFLOAT ? 16 : 4;
    const VkDevice device = device_;
    if (!physical || !device || !queue) {
        throw std::runtime_error("Invalid Vulkan texture device");
    }
    VkPhysicalDeviceProperties limits{};
    vkGetPhysicalDeviceProperties(physical, &limits);
    // Bound both the upload and arithmetic before multiplying by four.
    if (!width || !height || width > limits.limits.maxImageDimension2D ||
        height > limits.limits.maxImageDimension2D || width > INT32_MAX || height > INT32_MAX ||
        (cubemap_ && (width != height || width > limits.limits.maxImageDimensionCube)) ||
        uint64_t(width) * height * layers > 64ull * 1024 * 1024 ||
        uint64_t(width) * height * layers * bytesPerPixel != bytes || !pixels) {
        throw std::runtime_error("Invalid Vulkan texture dimensions or pixel data");
    }
    mipLevels_ = 1;
    if (mipmaps) {
        for (uint32_t side = std::max(width, height); side > 1; side /= 2) {
            ++mipLevels_;
        }
    }
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(physical, pixelFormat, &format);
    VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (mipLevels_ > 1) {
        required |= VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    }
    if ((format.optimalTilingFeatures & required) != required) {
        throw std::runtime_error("Vulkan texture format features are unsupported");
    }
    UploadResources temp{device};
    try {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &bi, nullptr, &temp.buffer), "vkCreateBuffer");
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, temp.buffer, &req);
        uint32_t type = memoryType(physical,
                                   req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkPhysicalDeviceMemoryProperties props{};
        vkGetPhysicalDeviceMemoryProperties(physical, &props);
        const bool coherent =
            (props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = type;
        check(vkAllocateMemory(device_, &ai, nullptr, &temp.memory), "vkAllocateMemory staging");
        check(vkBindBufferMemory(device_, temp.buffer, temp.memory, 0), "vkBindBufferMemory");
        void* mapped = nullptr;
        check(vkMapMemory(device_, temp.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
        std::memcpy(mapped, pixels, bytes);
        if (!coherent) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = temp.memory;
            range.size = VK_WHOLE_SIZE;
            VkResult status = vkFlushMappedMemoryRanges(device_, 1, &range);
            vkUnmapMemory(device_, temp.memory);
            check(status, "vkFlushMappedMemoryRanges");
        } else {
            vkUnmapMemory(device_, temp.memory);
        }

        VkImageCreateInfo image{};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.flags = cubemap_ ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        image.format = pixelFormat;
        image.extent = {width, height, 1};
        image.mipLevels = mipLevels_;
        image.arrayLayers = layers;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (mipLevels_ > 1) {
            image.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device_, &image, nullptr, &image_), "vkCreateImage");
        vkGetImageMemoryRequirements(device_, image_, &req);
        ai.allocationSize = req.size;
        ai.memoryTypeIndex =
            memoryType(physical, req.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_, &ai, nullptr, &memory_), "vkAllocateMemory image");
        check(vkBindImageMemory(device_, image_, memory_, 0), "vkBindImageMemory");
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image_;
        view.viewType = cubemap_ ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        view.format = image.format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = mipLevels_;
        view.subresourceRange.layerCount = layers;
        check(vkCreateImageView(device_, &view, nullptr, &view_), "vkCreateImageView");
        VkSamplerCreateInfo sampler{};
        sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter = nearestMagnification ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = address == Address::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                         : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler.addressModeV = sampler.addressModeW = address == Address::Repeat
                                                          ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                          : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = static_cast<float>(mipLevels_ - 1);
        check(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "vkCreateSampler");

        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = queueFamily;
        check(vkCreateCommandPool(device_, &pool, nullptr, &temp.pool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = temp.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device_, &alloc, &cmd), "vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = mipLevels_;
        barrier.subresourceRange.layerCount = layers;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = layers;
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(
            cmd, temp.buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        int32_t w = static_cast<int32_t>(width), h = static_cast<int32_t>(height);
        barrier.subresourceRange.levelCount = 1;
        for (uint32_t level = 1; level < mipLevels_; ++level) {
            barrier.subresourceRange.baseMipLevel = level - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &barrier);
            int32_t nextW = std::max(1, w / 2), nextH = std::max(1, h / 2);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, layers};
            blit.srcOffsets[1] = {w, h, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, layers};
            blit.dstOffsets[1] = {nextW, nextH, 1};
            vkCmdBlitImage(cmd,
                           image_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &blit,
                           VK_FILTER_LINEAR);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &barrier);
            w = nextW;
            h = nextH;
        }
        barrier.subresourceRange.baseMipLevel = mipLevels_ - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
        check(vkQueueWaitIdle(queue), "vkQueueWaitIdle");
    } catch (...) {
        vkDeviceWaitIdle(device_);
        cleanup();
        throw;
    }
}
VulkanTexture::~VulkanTexture() {
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
    cleanup();
}
void VulkanTexture::cleanup() noexcept {
    if (sampler_) {
        vkDestroySampler(device_, sampler_, nullptr);
    }
    if (view_) {
        vkDestroyImageView(device_, view_, nullptr);
    }
    if (image_) {
        vkDestroyImage(device_, image_, nullptr);
    }
    if (memory_) {
        vkFreeMemory(device_, memory_, nullptr);
    }
    sampler_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
}
