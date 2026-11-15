#include "rendering/vulkan_fsr3_game.hpp"
#include "ffx_host_vk.h"
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::mutex activeMutex;
bool active = false;
void check(VkResult result, const char* where) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(where) + " (Vulkan " + std::to_string(result) + ")");
    }
}
void checkFfx(FfxErrorCode result, const char* where) {
    if (result != FFX_OK) {
        throw std::runtime_error(std::string(where) + " (FSR " + std::to_string(result) + ")");
    }
}
VkImageCreateInfo
imageDescription(VkFormat format, TemporalSampling::Extent extent, VkImageUsageFlags usage) {
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {extent.width, extent.height, 1};
    ci.mipLevels = ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return ci;
}
void barrier(VkCommandBuffer cmd,
             VkImage image,
             VkImageAspectFlags aspect,
             VkImageLayout oldLayout,
             VkImageLayout newLayout) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
                          ? 0
                          : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &b);
}
FfxResource
resource(VkImage image, VkImageCreateInfo ci, const wchar_t* name, bool writable = false) {
    return ffxGetResourceVK(
        reinterpret_cast<void*>(image),
        ffxGetImageResourceDescriptionVK(
            image, ci, writable ? FFX_RESOURCE_USAGE_UAV : FFX_RESOURCE_USAGE_READ_ONLY),
        name,
        writable ? FFX_RESOURCE_STATE_UNORDERED_ACCESS : FFX_RESOURCE_STATE_COMPUTE_READ);
}
} // namespace

struct VulkanFsr3Game::Impl {
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageCreateInfo info{};
    };
    Device d{};
    TemporalSampling::Extent render{}, display{};
    FfxHostVkFunctions host{};
    std::unique_ptr<FfxFsr3UpscalerContext> context;
    void* scratch = nullptr;
    bool contextCreated = false, claimed = false, initializedImages = false, failed = false;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    Image output, dilatedDepth, dilatedMotion, previousDepth;

    ~Impl() {
        if (d.device) {
            vkDeviceWaitIdle(d.device);
        }
        if (contextCreated) {
            ffxFsr3UpscalerContextDestroy(context.get());
        }
        for (auto* i : {&output, &dilatedDepth, &dilatedMotion, &previousDepth}) {
            if (i->image) {
                vkDestroyImage(d.device, i->image, nullptr);
            }
            if (i->memory) {
                vkFreeMemory(d.device, i->memory, nullptr);
            }
        }
        if (pool) {
            vkDestroyCommandPool(d.device, pool, nullptr);
        }
        std::free(scratch);
        if (claimed) {
            std::lock_guard<std::mutex> lock(activeMutex);
            active = false;
        }
    }
    void createImage(Image& image,
                     VkFormat format,
                     TemporalSampling::Extent extent,
                     VkImageUsageFlags usage) {
        image.info = imageDescription(format, extent, usage);
        check(vkCreateImage(d.device, &image.info, nullptr, &image.image), "Create FSR image");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(d.device, image.image, &req);
        VkPhysicalDeviceMemoryProperties props{};
        vkGetPhysicalDeviceMemoryProperties(d.physical, &props);
        uint32_t chosen = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            if ((req.memoryTypeBits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                chosen = i;
                break;
            }
        }
        if (chosen == UINT32_MAX) {
            throw std::runtime_error("No device-local FSR image memory");
        }
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = chosen;
        check(vkAllocateMemory(d.device, &alloc, nullptr, &image.memory), "Allocate FSR image");
        check(vkBindImageMemory(d.device, image.image, image.memory, 0), "Bind FSR image");
    }
    void initialize(Device device, TemporalSampling::Extent r, TemporalSampling::Extent o) {
        d = device;
        render = r;
        display = o;
        if (!d.instance || !d.physical || !d.device || !d.queue ||
            d.apiVersion < VK_API_VERSION_1_2 || !r.width || !r.height || !o.width || !o.height ||
            r.width > o.width || r.height > o.height) {
            throw std::runtime_error("FSR requires a Vulkan 1.2 device and valid extents");
        }
        {
            std::lock_guard<std::mutex> lock(activeMutex);
            if (active) {
                throw std::runtime_error("FSR backend is already in use");
            }
            active = claimed = true;
        }
        auto fn = [&](const char* name) { return vkGetInstanceProcAddr(d.instance, name); };
        host.GetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            fn("vkGetPhysicalDeviceProperties"));
        host.GetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            fn("vkGetPhysicalDeviceProperties2"));
        host.GetPhysicalDeviceFeatures =
            reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(fn("vkGetPhysicalDeviceFeatures"));
        host.GetPhysicalDeviceFeatures2 =
            reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(fn("vkGetPhysicalDeviceFeatures2"));
        host.GetPhysicalDeviceMemoryProperties =
            reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
                fn("vkGetPhysicalDeviceMemoryProperties"));
        host.EnumerateDeviceExtensionProperties =
            reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
                fn("vkEnumerateDeviceExtensionProperties"));
        host.GetDeviceProcAddr =
            reinterpret_cast<PFN_vkGetDeviceProcAddr>(fn("vkGetDeviceProcAddr"));
        host.CreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(fn("vkCreateBuffer"));
        if (!host.GetPhysicalDeviceProperties || !host.GetPhysicalDeviceProperties2 ||
            !host.GetPhysicalDeviceFeatures || !host.GetPhysicalDeviceFeatures2 ||
            !host.GetPhysicalDeviceMemoryProperties || !host.EnumerateDeviceExtensionProperties ||
            !host.GetDeviceProcAddr || !host.CreateBuffer) {
            throw std::runtime_error("Vulkan FSR function unavailable");
        }
        ffxHostVkSet(host, {}); // This device enables no optional FSR features.
        VkPhysicalDeviceProperties properties{};
        host.GetPhysicalDeviceProperties(d.physical, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_2 ||
            o.width > properties.limits.maxImageDimension2D ||
            o.height > properties.limits.maxImageDimension2D) {
            throw std::runtime_error("Vulkan device does not support FSR extents or Vulkan 1.2");
        }
        VkPhysicalDeviceSubgroupProperties subgroup{};
        subgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &subgroup;
        host.GetPhysicalDeviceProperties2(d.physical, &properties2);
        constexpr VkSubgroupFeatureFlags requiredOperations =
            VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
            VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
            VK_SUBGROUP_FEATURE_QUAD_BIT;
        if (!(subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) ||
            (subgroup.supportedOperations & requiredOperations) != requiredOperations) {
            throw std::runtime_error(
                "Vulkan compute subgroup operations required by FSR are unavailable");
        }
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d.physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(d.physical, &count, queues.data());
        if (d.queueFamily >= count || !(queues[d.queueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            throw std::runtime_error("Game graphics queue cannot dispatch compute");
        }
        auto requireFormat = [&](VkFormat format, VkFormatFeatureFlags flags) {
            VkFormatProperties p{};
            vkGetPhysicalDeviceFormatProperties(d.physical, format, &p);
            if ((p.optimalTilingFeatures & flags) != flags) {
                throw std::runtime_error("Vulkan FSR image format not supported");
            }
        };
        requireFormat(VK_FORMAT_R16G16B16A16_SFLOAT,
                      VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                          VK_FORMAT_FEATURE_BLIT_SRC_BIT);
        requireFormat(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_BLIT_DST_BIT);
        requireFormat(VK_FORMAT_R32_SFLOAT,
                      VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
        requireFormat(VK_FORMAT_R16G16_SFLOAT,
                      VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
        requireFormat(VK_FORMAT_R32_UINT,
                      VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
        size_t size = ffxGetScratchMemorySizeVK(d.physical, FFX_FSR3UPSCALER_CONTEXT_COUNT);
        if (!size || size > SIZE_MAX - 63) {
            throw std::runtime_error("Invalid FSR scratch size");
        }
        scratch = std::aligned_alloc(64, (size + 63) / 64 * 64);
        if (!scratch) {
            throw std::bad_alloc();
        }
        std::memset(scratch, 0, size);
        VkDeviceContext vkdc{d.device, d.physical, host.GetDeviceProcAddr};
        FfxFsr3UpscalerContextDescription cd{};
        checkFfx(ffxGetInterfaceVK(&cd.backendInterface,
                                   ffxGetDeviceVK(&vkdc),
                                   scratch,
                                   size,
                                   FFX_FSR3UPSCALER_CONTEXT_COUNT),
                 "Initialize FSR backend");
        cd.flags = FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
        cd.maxRenderSize = {r.width, r.height};
        cd.maxUpscaleSize = {o.width, o.height};
        context = std::make_unique<FfxFsr3UpscalerContext>();
        checkFfx(ffxFsr3UpscalerContextCreate(context.get(), &cd), "Create FSR context");
        contextCreated = true;
        auto rw = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        createImage(output,
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    o,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        createImage(dilatedDepth, VK_FORMAT_R32_SFLOAT, r, rw);
        createImage(dilatedMotion, VK_FORMAT_R16G16_SFLOAT, r, rw);
        createImage(previousDepth, VK_FORMAT_R32_UINT, r, rw);
        VkCommandPoolCreateInfo pc{};
        pc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pc.queueFamilyIndex = d.queueFamily;
        check(vkCreateCommandPool(d.device, &pc, nullptr, &pool), "Create FSR command pool");
        VkCommandBufferAllocateInfo ca{};
        ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ca.commandPool = pool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(d.device, &ca, &commands), "Allocate FSR commands");
    }
    void dispatch(const Inputs& in) {
        if (failed || !in.color || !in.depth || !in.motion || !in.destination ||
            !std::isfinite(in.frameTimeMs) || in.frameTimeMs <= 0 ||
            !std::isfinite(in.cameraNear) || !std::isfinite(in.cameraFar) ||
            !std::isfinite(in.cameraFovRadians) || in.cameraNear <= 0 ||
            in.cameraFar <= in.cameraNear || in.cameraFovRadians <= 0 ||
            in.cameraFovRadians >= 3.1415926f) {
            throw std::runtime_error("Invalid FSR dispatch input");
        }
        try {
            check(vkResetCommandBuffer(commands, 0), "Reset FSR commands");
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(vkBeginCommandBuffer(commands, &begin), "Begin FSR commands");
            barrier(commands,
                    in.color,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            barrier(commands,
                    in.depth,
                    VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (!initializedImages) {
                for (auto* i : {&output, &dilatedDepth, &dilatedMotion, &previousDepth}) {
                    barrier(commands,
                            i->image,
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL);
                }
            }
            auto colorInfo =
                imageDescription(VK_FORMAT_R8G8B8A8_UNORM,
                                 render,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            auto depthInfo = imageDescription(VK_FORMAT_D32_SFLOAT,
                                              render,
                                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                  VK_IMAGE_USAGE_SAMPLED_BIT);
            auto motionInfo =
                imageDescription(VK_FORMAT_R32G32_SFLOAT,
                                 render,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            FfxFsr3UpscalerDispatchDescription dd{};
            dd.commandList = ffxGetCommandListVK(commands);
            dd.color = resource(in.color, colorInfo, L"Game color");
            dd.depth = resource(in.depth, depthInfo, L"Game depth");
            dd.motionVectors = resource(in.motion, motionInfo, L"Game motion");
            dd.output = resource(output.image, output.info, L"Game output", true);
            dd.dilatedDepth =
                resource(dilatedDepth.image, dilatedDepth.info, L"Dilated depth", true);
            dd.dilatedMotionVectors =
                resource(dilatedMotion.image, dilatedMotion.info, L"Dilated motion", true);
            dd.reconstructedPrevNearestDepth =
                resource(previousDepth.image, previousDepth.info, L"Previous depth", true);
            dd.jitterOffset = {in.jitter.x, in.jitter.y};
            dd.motionVectorScale = {float(render.width), float(render.height)};
            dd.renderSize = {render.width, render.height};
            dd.upscaleSize = {display.width, display.height};
            dd.frameTimeDelta = in.frameTimeMs;
            dd.preExposure = 1;
            dd.reset = in.reset;
            dd.cameraNear = in.cameraNear;
            dd.cameraFar = in.cameraFar;
            dd.cameraFovAngleVertical = in.cameraFovRadians;
            dd.viewSpaceToMetersFactor = 1;
            checkFfx(ffxFsr3UpscalerContextDispatch(context.get(), &dd), "Dispatch FSR");
            barrier(commands,
                    in.depth,
                    VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            barrier(commands,
                    output.image,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            barrier(commands,
                    in.destination,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstSubresource = blit.srcSubresource;
            blit.srcOffsets[1] = {int32_t(display.width), int32_t(display.height), 1};
            blit.dstOffsets[1] = blit.srcOffsets[1];
            vkCmdBlitImage(commands,
                           output.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           in.destination,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &blit,
                           VK_FILTER_NEAREST);
            barrier(commands,
                    in.destination,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            barrier(commands,
                    output.image,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL);
            check(vkEndCommandBuffer(commands), "End FSR commands");
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &commands;
            check(vkQueueSubmit(d.queue, 1, &si, VK_NULL_HANDLE), "Submit FSR commands");
            check(vkQueueWaitIdle(d.queue), "Wait for FSR commands");
            initializedImages = true;
        } catch (...) {
            failed = true;
            throw;
        }
    }
};
VulkanFsr3Game::VulkanFsr3Game(Device d, TemporalSampling::Extent r, TemporalSampling::Extent o)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(d, r, o);
}
VulkanFsr3Game::~VulkanFsr3Game() = default;
void VulkanFsr3Game::dispatch(const Inputs& in) {
    impl_->dispatch(in);
}
